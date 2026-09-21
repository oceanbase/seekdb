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

#include "share/plugin/ob_plugin_loader.h"
#include "seekdb/plugin/server_dev.h"
#include "share/plugin/extension_package.h"
#include "share/plugin/catalog_builder.h"
#include "share/plugin/extension_install.h"
#include "seekdb/plugin/catalog_spi.h"
#include "seekdb/plugin/memory_spi.h"
#include <thread>
#include "plugin_runtime.h"
#include "seekdb/plugin/sql_spi.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <set>
#include <utility>

#include "lib/ob_errno.h"
#include "seekdb/plugin/extension_spi.h"
#include "share/rc/ob_module_provider.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace oceanbase
{
namespace share
{
namespace plugin
{

using namespace oceanbase::common;

namespace
{

const uint32_t MAX_PLUGIN_STRING = SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES;
const uint32_t MAX_SERVICE_COUNT = SEEKDB_PLUGIN_MAX_SERVICES;
const uint32_t MAX_EXTENSION_COUNT = SEEKDB_PLUGIN_MAX_EXTENSIONS;
const uint64_t MAX_CONVERTED_ARGUMENT_BYTES = UINT64_C(16777216);
const seekdb_plugin_capability_t KNOWN_RUNTIME_CAPABILITIES =
    SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE |
    SEEKDB_PLUGIN_CAPABILITY_MULTI_INSTANCE |
    SEEKDB_PLUGIN_CAPABILITY_SIDE_BY_SIDE_UPGRADE |
    SEEKDB_PLUGIN_CAPABILITY_PERSISTENT_DATA |
    SEEKDB_PLUGIN_CAPABILITY_TRANSACTIONAL_SERVICES;
const seekdb_plugin_capability_t KNOWN_SERVICE_CAPABILITIES =
    KNOWN_RUNTIME_CAPABILITIES |
    SEEKDB_PLUGIN_CAPABILITY_EXTENSION_CATALOG;
const seekdb_plugin_extension_flags_t KNOWN_EXTENSION_FLAGS =
    SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
    SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE |
    SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING |
    SEEKDB_PLUGIN_EXTENSION_FLAG_PERSISTENT |
    SEEKDB_PLUGIN_EXTENSION_FLAG_PARALLEL_SAFE |
    SEEKDB_PLUGIN_EXTENSION_FLAG_REQUIRES_CATALOG;

struct StagedService
{
  ObPluginServiceSpec spec_;
};

// Own conversion output before the plugin's callback-local buffer disappears.
// Failure is sticky even when a faulty callback ignores emit's return status.
struct ConvertedArgument
{
  std::string source_type_;
  std::string target_type_;
  std::vector<uint8_t> bytes_;
  seekdb_plugin_execution_value_v1_t value_ = {};
  ObPluginExtensionLease object_;
  ObPluginLease implementation_;
  seekdb_plugin_instance_handle_t *instance_ = nullptr;
  uint64_t byte_limit_ = 0;
  bool emitted_ = false;
  seekdb_plugin_status_t error_ = SEEKDB_PLUGIN_STATUS_OK;
};

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_converted_argument(
    seekdb_plugin_host_handle_t *host, const seekdb_plugin_execution_result_v1_t *value)
{
  if (!host) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  auto &sink = *reinterpret_cast<ConvertedArgument *>(host);
  if (sink.error_ != SEEKDB_PLUGIN_STATUS_OK) return sink.error_;
  if (sink.emitted_) return sink.error_ = SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  sink.emitted_ = true;
  if (!value || value->struct_size < sizeof(*value) || (!value->is_null &&
      (!value->type_id || std::strncmp(value->type_id, sink.target_type_.c_str(),
          sink.target_type_.size() + 1) != 0 || value->data_size > sink.byte_limit_ ||
       (value->data_size && !value->data)))) {
    return sink.error_ = SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  try {
    if (!value->is_null && value->data_size) sink.bytes_.assign(value->data, value->data + value->data_size);
    sink.value_ = {};
    sink.value_.struct_size = sizeof(sink.value_);
    sink.value_.type_id = sink.target_type_.c_str();
    sink.value_.is_null = value->is_null;
    sink.value_.data = sink.bytes_.empty() ? nullptr : sink.bytes_.data();
    sink.value_.data_size = sink.bytes_.size();
  } catch (const std::bad_alloc &) { sink.error_ = SEEKDB_PLUGIN_STATUS_NO_MEMORY;
  } catch (...) { sink.error_ = SEEKDB_PLUGIN_STATUS_INTERNAL; }
  return sink.error_;
}

struct HostContext;

struct HostLease
{
  explicit HostLease(HostContext *host) : host_(host), lease_() {}
  HostContext *host_;
  ObPluginLease lease_;
};

struct HostContext
{
  explicit HostContext(PluginMemoryLimits memory_limits)
      : registry_(nullptr), owner_(), api_(), mutex_(), leases_(),
        memory_(seekdb_runtime_memory_create(memory_limits.bytes_, memory_limits.allocations_),
                seekdb_runtime_memory_destroy),
        registration_(seekdb_runtime_registration_create(), seekdb_runtime_registration_destroy)
  {
    if (!memory_ || !registration_) throw std::bad_alloc();
  }

  ObPluginServiceRegistry *registry_;
  std::shared_ptr<ObPluginGeneration> owner_;
  seekdb_plugin_host_api_v3_t api_;
  std::mutex mutex_;
  std::set<HostLease *> leases_;
  std::unique_ptr<seekdb_runtime_memory_account, decltype(&seekdb_runtime_memory_destroy)> memory_;
  std::unique_ptr<seekdb_runtime_registration, decltype(&seekdb_runtime_registration_destroy)> registration_;
  // Immutable adapter snapshots materialized only AFTER the Rust journal seals.
  // There is no second C++ transaction collection or quota/commit state.
  std::vector<StagedService> staged_;
  std::vector<ObPluginExtensionSpec> staged_extensions_;
};

bool all_zero(const uint64_t *values, const size_t count)
{
  bool zero = true;
  for (size_t i = 0; zero && i < count; ++i) {
    zero = values[i] == 0;
  }
  return zero;
}

bool bounded_string(const char *value, const size_t maximum, size_t &length,
                    const bool allow_empty = false)
{
  bool valid = nullptr != value;
  length = 0;
  while (valid && length <= maximum && value[length] != '\0') {
    ++length;
  }
  return valid && (allow_empty || length > 0) && length <= maximum;
}

bool valid_identifier(const char *value)
{
  size_t length = 0;
  bool valid = bounded_string(value, MAX_PLUGIN_STRING, length);
  for (size_t i = 0; valid && i < length; ++i) {
    const char c = value[i];
    valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '.' || c == '_' || c == '-';
  }
  return valid;
}

bool valid_bounded_text(const std::string &value,
                        const size_t maximum,
                        const bool allow_empty = false)
{
  return (allow_empty || !value.empty()) && value.size() <= maximum &&
         value.find('\0') == std::string::npos;
}

bool valid_identifier(const std::string &value)
{
  bool valid = valid_bounded_text(value, MAX_PLUGIN_STRING);
  for (size_t i = 0; valid && i < value.size(); ++i) {
    const char c = value[i];
    valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '.' || c == '_' || c == '-';
  }
  return valid;
}

int compare_version(const seekdb_plugin_semantic_version_t &left,
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

bool same_version(const seekdb_plugin_semantic_version_t &left,
                  const seekdb_plugin_semantic_version_t &right)
{
  return 0 == compare_version(left, right);
}

void assign_error_noexcept(std::string &error, const char *message) noexcept
{
  try {
    error = nullptr == message ? "plugin operation failed" : message;
  } catch (...) {
    error.clear();
  }
}

void append_error_noexcept(std::string &error, const char *message) noexcept
{
  try {
    error += nullptr == message ? "; plugin operation failed" : message;
  } catch (...) {
  }
}

bool unbounded(const seekdb_plugin_semantic_version_t &version)
{
  return version.major == 0 && version.minor == 0 && version.patch == 0;
}

bool version_in_range(const seekdb_plugin_semantic_version_t &version,
                      const seekdb_plugin_version_range_t &range)
{
  return version.major == range.minimum_inclusive.major &&
      compare_version(version, range.minimum_inclusive) >= 0 &&
      (unbounded(range.maximum_exclusive) ||
       compare_version(version, range.maximum_exclusive) < 0);
}

bool valid_range(const seekdb_plugin_version_range_t &range)
{
  const size_t required_size = sizeof(seekdb_plugin_version_range_t);
  bool valid_maximum = unbounded(range.maximum_exclusive);
  if (!valid_maximum &&
      range.maximum_exclusive.major == range.minimum_inclusive.major) {
    valid_maximum =
        compare_version(range.minimum_inclusive, range.maximum_exclusive) < 0;
  } else if (!valid_maximum &&
             range.minimum_inclusive.major <
                 std::numeric_limits<uint32_t>::max()) {
    valid_maximum =
        range.maximum_exclusive.major == range.minimum_inclusive.major + 1 &&
        0 == range.maximum_exclusive.minor &&
        0 == range.maximum_exclusive.patch;
  }
  return range.struct_size == required_size && valid_maximum &&
      all_zero(range.reserved,
               sizeof(range.reserved) / sizeof(range.reserved[0]));
}

seekdb_plugin_status_t to_plugin_status(const int ret)
{
  seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_INTERNAL;
  switch (ret) {
    case OB_SUCCESS: status = SEEKDB_PLUGIN_STATUS_OK; break;
    case OB_INVALID_ARGUMENT: status = SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT; break;
    case OB_NOT_SUPPORTED:
    case OB_ERROR_FUNC_VERSION: status = SEEKDB_PLUGIN_STATUS_UNSUPPORTED_ABI; break;
    case OB_ENTRY_NOT_EXIST:
    case OB_FILE_NOT_EXIST: status = SEEKDB_PLUGIN_STATUS_NOT_FOUND; break;
    case OB_ENTRY_EXIST:
    case OB_INIT_TWICE: status = SEEKDB_PLUGIN_STATUS_ALREADY_EXISTS; break;
    case OB_ALLOCATE_MEMORY_FAILED: status = SEEKDB_PLUGIN_STATUS_NO_MEMORY; break;
    case OB_STATE_NOT_MATCH:
    case OB_NOT_INIT: status = SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION; break;
    case OB_EAGAIN: status = SEEKDB_PLUGIN_STATUS_BUSY; break;
    case OB_TIMEOUT:
    case OB_IO_ERROR: status = SEEKDB_PLUGIN_STATUS_UNAVAILABLE; break;
    case OB_INVALID_DATA:
    case OB_SIZE_OVERFLOW: status = SEEKDB_PLUGIN_STATUS_INVALID_MANIFEST; break;
    default: status = SEEKDB_PLUGIN_STATUS_INTERNAL; break;
  }
  return status;
}

int from_plugin_status(const seekdb_plugin_status_t status)
{
  int ret = OB_ERR_UNEXPECTED;
  switch (status) {
    case SEEKDB_PLUGIN_STATUS_OK: ret = OB_SUCCESS; break;
    case SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT: ret = OB_INVALID_ARGUMENT; break;
    case SEEKDB_PLUGIN_STATUS_UNSUPPORTED_ABI: ret = OB_NOT_SUPPORTED; break;
    case SEEKDB_PLUGIN_STATUS_NOT_FOUND: ret = OB_ENTRY_NOT_EXIST; break;
    case SEEKDB_PLUGIN_STATUS_ALREADY_EXISTS: ret = OB_ENTRY_EXIST; break;
    case SEEKDB_PLUGIN_STATUS_NO_MEMORY: ret = OB_ALLOCATE_MEMORY_FAILED; break;
    case SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION: ret = OB_STATE_NOT_MATCH; break;
    case SEEKDB_PLUGIN_STATUS_BUSY: ret = OB_EAGAIN; break;
    case SEEKDB_PLUGIN_STATUS_UNAVAILABLE: ret = OB_IO_ERROR; break;
    case SEEKDB_PLUGIN_STATUS_INTERNAL: ret = OB_ERR_UNEXPECTED; break;
    case SEEKDB_PLUGIN_STATUS_PERMISSION_DENIED: ret = OB_ERROR; break;
    case SEEKDB_PLUGIN_STATUS_INVALID_MANIFEST: ret = OB_INVALID_DATA; break;
    case SEEKDB_PLUGIN_STATUS_DEPENDENCY_CYCLE: ret = OB_INVALID_DATA; break;
    case SEEKDB_PLUGIN_STATUS_TIMEOUT: ret = OB_TIMEOUT; break;
    case SEEKDB_PLUGIN_STATUS_VERIFY_FAILED: ret = OB_CHECKSUM_ERROR; break;
    case SEEKDB_PLUGIN_STATUS_MIGRATION_FAILED: ret = OB_ERROR; break;
    case SEEKDB_PLUGIN_STATUS_END_OF_STREAM: ret = OB_ITER_END; break;
    default: ret = OB_ERR_UNEXPECTED; break;
  }
  return ret;
}

int validate_function_lease(const ObPluginLease &lease)
{
  if (!lease.is_valid() || !lease.service() || lease.service_minor() < SEEKDB_PLUGIN_EXECUTION_SPI_MINOR)
    return OB_STATE_NOT_MATCH;
  const auto *service = static_cast<const seekdb_plugin_function_service_v1_t *>(lease.service());
  if (service->struct_size < sizeof(*service) || service->spi_major != SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR ||
      service->spi_minor < SEEKDB_PLUGIN_EXECUTION_SPI_MINOR || !service->execute || service->reserved_word ||
      !all_zero(service->reserved, 8)) return OB_NOT_SUPPORTED;
  return OB_SUCCESS;
}

int execute_pinned_function(ObPluginLease &lease, seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments, uint32_t count)
{
  if (!context || context->struct_size < sizeof(*context) || !instance) return OB_INVALID_ARGUMENT;
  int ret = validate_function_lease(lease);
  if (ret != OB_SUCCESS) return ret;
  const auto *service = static_cast<const seekdb_plugin_function_service_v1_t *>(lease.service());
  auto legacy = *context;
  if (service->spi_minor < SEEKDB_PLUGIN_EXECUTION_SQL_CONTEXT_MINOR && context->struct_size > sizeof(*context)) {
    legacy.struct_size = sizeof(legacy); context = &legacy;
  }
  try { return from_plugin_status(service->execute(instance, context, arguments, count));
  } catch (...) { return OB_ERR_UNEXPECTED; }
}

bool valid_execution_argument(const seekdb_plugin_execution_value_v1_t &value)
{
  return value.struct_size >= sizeof(value) && value.is_null <= 1 &&
      std::all_of(std::begin(value.reserved_bytes), std::end(value.reserved_bytes), [](uint8_t b) { return b == 0; }) &&
      all_zero(value.reserved, 4) &&
      (value.type_id ? valid_identifier(value.type_id) : value.is_null) &&
      (value.is_null || (value.data_size <= MAX_CONVERTED_ARGUMENT_BYTES && (!value.data_size || value.data)));
}

// Conversion leases and instance pointers are prepared once. A table cursor
// retains them, allowing rescan without a loader pointer or another selection.
bool null_propagating_table_input(seekdb_plugin_extension_flags_t flags,
    const seekdb_plugin_execution_value_v1_t *arguments, uint32_t count)
{
  if (!(flags & SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING)) return false;
  for (uint32_t i = 0; i < count; ++i) if (arguments[i].is_null) return true;
  return false;
}

struct BatchResultSink
{
  struct Row { std::vector<uint8_t> bytes_; bool null_ = false; bool emitted_ = false; };
  const char *type_;
  std::vector<Row> rows_;
  uint64_t bytes_ = 0;
  seekdb_plugin_status_t error_ = SEEKDB_PLUGIN_STATUS_OK;
};
int get_batch_function_service(const seekdb_plugin_function_service_v1_t *base,
    const seekdb_plugin_function_service_v3_t *&batch)
{
  batch = nullptr;
  if (!base || base->struct_size < sizeof(*base) || base->spi_major != SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR ||
      !base->execute || base->reserved_word || !all_zero(base->reserved, 8)) return OB_NOT_SUPPORTED;
  if (base->spi_minor < SEEKDB_PLUGIN_EXECUTION_BATCH_MINOR) return OB_SUCCESS;
  if (base->struct_size < sizeof(seekdb_plugin_function_service_v3_t)) return OB_NOT_SUPPORTED;
  const auto *suffix = reinterpret_cast<const seekdb_plugin_function_service_v3_t *>(base);
  if (!suffix->execute_batch || !all_zero(suffix->v2.resolution_reserved, 4) ||
      !all_zero(suffix->batch_reserved, 4)) return OB_NOT_SUPPORTED;
  batch = suffix;
  return OB_SUCCESS;
}
seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_batch_result(
    seekdb_plugin_host_handle_t *host, uint32_t index,
    const seekdb_plugin_execution_result_v1_t *value)
{
  if (!host) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  auto &sink = *reinterpret_cast<BatchResultSink *>(host);
  if (sink.error_ != SEEKDB_PLUGIN_STATUS_OK) return sink.error_;
  if (index >= sink.rows_.size()) return sink.error_ = SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  auto &row = sink.rows_[index];
  if (row.emitted_) return sink.error_ = SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  row.emitted_ = true;
  if (!value || value->struct_size < sizeof(*value) || value->is_null > 1 ||
      !valid_identifier(value->type_id) || std::strcmp(value->type_id, sink.type_) ||
      !all_zero(value->reserved, 4) ||
      std::any_of(std::begin(value->reserved_bytes), std::end(value->reserved_bytes), [](uint8_t b) { return b; }) ||
      value->data_size > MAX_CONVERTED_ARGUMENT_BYTES ||
      (!value->is_null && value->data_size && !value->data))
    return sink.error_ = SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  const uint64_t size = value->is_null ? 0 : value->data_size;
  if (size > SEEKDB_PLUGIN_MAX_BATCH_BYTES - sink.bytes_) return sink.error_ = SEEKDB_PLUGIN_STATUS_NO_MEMORY;
  try {
    if (size) row.bytes_.assign(value->data, value->data + size);
    row.null_ = value->is_null; sink.bytes_ += size;
  } catch (const std::bad_alloc &) { return sink.error_ = SEEKDB_PLUGIN_STATUS_NO_MEMORY;
  } catch (...) { return sink.error_ = SEEKDB_PLUGIN_STATUS_INTERNAL; }
  return SEEKDB_PLUGIN_STATUS_OK;
}
struct ScalarBatchSink { BatchResultSink *batch_; uint32_t row_; };
seekdb_plugin_status_t SEEKDB_PLUGIN_CALL reject_batch_scalar_result(
    seekdb_plugin_host_handle_t *host, const seekdb_plugin_execution_result_v1_t *)
{
  if (!host) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  auto &sink = *reinterpret_cast<BatchResultSink *>(host);
  if (sink.error_ == SEEKDB_PLUGIN_STATUS_OK) sink.error_ = SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  return sink.error_;
}
seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit_scalar_batch_result(
    seekdb_plugin_host_handle_t *host, const seekdb_plugin_execution_result_v1_t *value)
{
  if (!host) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  const auto &sink = *reinterpret_cast<ScalarBatchSink *>(host);
  return emit_batch_result(reinterpret_cast<seekdb_plugin_host_handle_t *>(sink.batch_), sink.row_, value);
}
int poll_batch_query(const seekdb_plugin_execution_context_v1_t *context)
{
  if (context->struct_size < sizeof(seekdb_plugin_execution_context_v2_t)) return OB_SUCCESS;
  const auto &query = *reinterpret_cast<const seekdb_plugin_execution_context_v2_t *>(context);
  if (!all_zero(query.reserved, 4)) return OB_INVALID_ARGUMENT;
  if (!query.sql_api) return OB_SUCCESS;
  const auto *base = query.sql_api;
  if (base->struct_size < sizeof(*base) || base->spi_major != SEEKDB_PLUGIN_SQL_SPI_MAJOR ||
      base->reserved_word || !all_zero(base->reserved, 6)) return OB_INVALID_ARGUMENT;
  if (base->spi_minor < 1) return OB_SUCCESS;
  if (base->struct_size < sizeof(seekdb_plugin_sql_api_v2_t)) return OB_INVALID_ARGUMENT;
  const auto &api = *reinterpret_cast<const seekdb_plugin_sql_api_v2_t *>(base);
  if (!all_zero(api.reserved, 4)) return OB_INVALID_ARGUMENT;
  if (!api.poll_query) return OB_SUCCESS;
  if (!query.sql_context) return OB_INVALID_ARGUMENT;
  seekdb_plugin_query_status_v1_t status{}; status.struct_size = sizeof(status); status.remaining_us = -1;
  int ret = from_plugin_status(api.poll_query(query.sql_context, &status));
  if (ret == OB_ITER_END) return OB_INVALID_DATA;
  if (status.struct_size != sizeof(status) || status.reserved_word || !all_zero(status.reserved, 4) ||
      status.remaining_us < -1 || status.database_error < INT32_MIN || status.database_error > 0) return OB_INVALID_DATA;
  return status.database_error ? static_cast<int>(status.database_error) : ret;
}

template <typename Consume>
int apply_prepared_arguments(std::vector<ConvertedArgument> &prepared,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments, uint32_t count, Consume consume)
try {
  if (!context || context->struct_size < sizeof(*context) || count != prepared.size() || (count && !arguments))
    return OB_INVALID_ARGUMENT;
  std::vector<seekdb_plugin_execution_value_v1_t> inputs(count);
  for (uint32_t i = 0; i < count; ++i) {
    const auto &input = arguments[i]; auto &item = prepared[i];
    if (!valid_execution_argument(input)) return OB_INVALID_ARGUMENT;
    if (item.source_type_ != (input.type_id ? input.type_id : "")) return OB_STATE_NOT_MATCH;
    inputs[i] = input; inputs[i].struct_size = sizeof(input);
    if (!input.type_id) inputs[i].type_id = item.target_type_.empty() ? nullptr : item.target_type_.c_str();
    if (input.is_null) { inputs[i].data = nullptr; inputs[i].data_size = 0; }
  }
  uint64_t total = 0;
  for (uint32_t i = 0; i < count; ++i) {
    auto &item = prepared[i];
    if (!item.implementation_.is_valid()) continue;
    item.bytes_.clear(); item.value_ = {}; item.emitted_ = false; item.error_ = SEEKDB_PLUGIN_STATUS_OK;
    item.byte_limit_ = MAX_CONVERTED_ARGUMENT_BYTES - total;
    seekdb_plugin_execution_context_v2_t cast_context = {};
    if (context->struct_size >= sizeof(cast_context)) {
      cast_context = *reinterpret_cast<const seekdb_plugin_execution_context_v2_t *>(context);
      cast_context.v1.struct_size = sizeof(cast_context);
    } else { cast_context.v1 = *context; cast_context.v1.struct_size = sizeof(cast_context.v1); }
    cast_context.v1.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&item);
    cast_context.v1.emit_result = emit_converted_argument;
    int ret = execute_pinned_function(item.implementation_, item.instance_, &cast_context.v1, &inputs[i], 1);
    // END_OF_STREAM belongs to table iteration, not scalar conversion. Never
    // turn a malformed cast status into a successfully empty table invocation.
    if (ret == OB_ITER_END) return OB_INVALID_DATA;
    if (ret != OB_SUCCESS) return ret;
    if (item.error_ != SEEKDB_PLUGIN_STATUS_OK) return from_plugin_status(item.error_);
    if (!item.emitted_) return OB_INVALID_DATA;
    inputs[i] = item.value_; total += item.bytes_.size();
  }
  return consume(inputs.empty() ? nullptr : inputs.data(), count);
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

// No loader/registry mutex is held here. Caller owns both object and code leases.
int resolve_function_result_type(const seekdb_plugin_function_service_v1_t *base,
    seekdb_plugin_instance_handle_t *instance, const char *const *types, uint32_t count,
    std::string &type_id)
{
  type_id.clear();
  if (!instance || count > SEEKDB_PLUGIN_MAX_ARGUMENTS || (count && !types)) return OB_INVALID_ARGUMENT;
  for (uint32_t i = 0; i < count; ++i) if (types[i] && !valid_identifier(types[i])) return OB_INVALID_ARGUMENT;
  if (!base || base->struct_size < sizeof(seekdb_plugin_function_service_v2_t) ||
      base->spi_major != SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR ||
      base->spi_minor < SEEKDB_PLUGIN_EXECUTION_RESULT_TYPE_MINOR ||
      base->reserved_word || !base->execute || !all_zero(base->reserved, 8)) return OB_NOT_SUPPORTED;
  const auto *service = reinterpret_cast<const seekdb_plugin_function_service_v2_t *>(base);
  if (!service->resolve_result || !all_zero(service->resolution_reserved, 4)) return OB_NOT_SUPPORTED;
  seekdb_plugin_resolved_type_v1_t result = {};
  result.struct_size = sizeof(result);
  try {
    const int ret = from_plugin_status(service->resolve_result(instance, types, count, &result));
    if (ret != OB_SUCCESS) return ret;
    if (result.struct_size != sizeof(result) || !all_zero(result.reserved, 4) ||
        !std::memchr(result.type_id, 0, sizeof(result.type_id)) || !valid_identifier(result.type_id)) return OB_INVALID_DATA;
    type_id.assign(result.type_id);
    return OB_SUCCESS;
  } catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) { return OB_ERR_UNEXPECTED; }
}

int validate_type_comparison_service(const seekdb_plugin_type_codec_service_v1_t *base,
    seekdb_plugin_type_compare_v1_fn &compare)
{
  compare = nullptr;
  if (!base || base->struct_size < sizeof(seekdb_plugin_type_codec_service_v2_t) ||
      base->spi_major != SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR || base->spi_minor < SEEKDB_PLUGIN_TYPE_COMPARISON_MINOR ||
      base->reserved_word || !base->decode || !base->encode || !all_zero(base->reserved, 8)) return OB_NOT_SUPPORTED;
  const auto *service = reinterpret_cast<const seekdb_plugin_type_codec_service_v2_t *>(base);
  if (!service->compare || !all_zero(service->comparison_reserved, 4)) return OB_NOT_SUPPORTED;
  compare = service->compare;
  return OB_SUCCESS;
}

// Caller owns the exact object/code leases and has validated decoded values.
// There is no registry lock or query/session context across the callback.
int invoke_type_comparison(seekdb_plugin_type_compare_v1_fn compare,
    seekdb_plugin_instance_handle_t *instance, const seekdb_plugin_execution_value_v1_t &left,
    const seekdb_plugin_execution_value_v1_t &right, int32_t &ordering)
{
  ordering = 0;
  if (!compare || !instance) return OB_INVALID_ARGUMENT;
  seekdb_plugin_type_comparison_v1_t result{}; result.struct_size = sizeof(result);
  int ret = OB_SUCCESS;
  try { ret = from_plugin_status(compare(instance, &left, &right, &result));
  } catch (const std::bad_alloc &) { ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) { ret = OB_ERR_UNEXPECTED; }
  // End-of-stream is a table protocol, never a comparison result. Letting it
  // escape could cause a future SQL consumer to silently truncate execution.
  if (ret == OB_ITER_END) return OB_INVALID_DATA;
  if (ret != OB_SUCCESS) return ret;
  if (result.struct_size != sizeof(result) || result.ordering < -1 || result.ordering > 1 ||
      !all_zero(result.reserved, 4)) return OB_INVALID_DATA;
  ordering = result.ordering;
  return OB_SUCCESS;
}

HostContext *as_host(seekdb_plugin_host_handle_t *opaque)
{
  return reinterpret_cast<HostContext *>(opaque);
}

void *SEEKDB_PLUGIN_CALL host_alloc(seekdb_plugin_host_handle_t *opaque,
                                    const uint64_t size,
                                    const uint32_t alignment)
{
  HostContext *host = as_host(opaque);
  return host == nullptr ? nullptr : seekdb_runtime_memory_alloc(host->memory_.get(), size, alignment);
}

void SEEKDB_PLUGIN_CALL host_free(seekdb_plugin_host_handle_t *opaque,
                                  void *memory,
                                  uint64_t size,
                                  uint32_t alignment)
{
  HostContext *host = as_host(opaque);
  if (host != nullptr) {
    // A void public callback cannot return diagnostics; mismatched frees remain
    // owned/charged and are visible in the generation's status snapshot.
    (void)seekdb_runtime_memory_free(host->memory_.get(), memory, size, alignment);
  }
}

void SEEKDB_PLUGIN_CALL release_owned_bytes(void *owner)
{
  seekdb_runtime_memory_buffer_destroy(static_cast<seekdb_runtime_memory_buffer *>(owner));
}

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL host_allocate_owned_bytes(
    seekdb_plugin_host_handle_t *opaque, uint64_t size, uint32_t alignment,
    seekdb_plugin_owned_bytes_v1_t *output)
{
  if (!output) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  *output = {};
  HostContext *host = as_host(opaque);
  if (!host || !size || !alignment || (alignment & (alignment - 1)) != 0 ||
      uint64_t(alignment - 1) > uint64_t(std::numeric_limits<ptrdiff_t>::max()) ||
      size > uint64_t(std::numeric_limits<ptrdiff_t>::max()) - (alignment - 1)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  auto *token = seekdb_runtime_memory_buffer_create(host->memory_.get(), size, alignment);
  if (!token) return SEEKDB_PLUGIN_STATUS_NO_MEMORY;
  output->struct_size = sizeof(*output);
  output->alignment = alignment; output->size = size;
  output->data = static_cast<uint8_t *>(seekdb_runtime_memory_buffer_data(token));
  output->owner = token; output->release = release_owned_bytes;
  return SEEKDB_PLUGIN_STATUS_OK;
}

void SEEKDB_PLUGIN_CALL host_log(seekdb_plugin_host_handle_t *,
                                 const seekdb_plugin_log_level_t level,
                                 const char *component,
                                 const char *message)
{
  try {
    const char *safe_component = nullptr == component ? "plugin" : component;
    const char *safe_message = nullptr == message ? "(null)" : message;
    std::fprintf(stderr, "[seekdb-plugin:%d] %s: %s\n",
                 static_cast<int>(level), safe_component, safe_message);
  } catch (...) {
  }
}

int validate_registration_service(const seekdb_plugin_service_provide_descriptor_t &service,
                                  StagedService &staged,
                                  std::string &error)
{
  int ret = OB_SUCCESS;
  const size_t required_size = sizeof(seekdb_plugin_service_provide_descriptor_t);
  if (service.struct_size != required_size || !valid_identifier(service.service_id) ||
      service.version.major == 0 || nullptr == service.service ||
      (service.capabilities & ~KNOWN_SERVICE_CAPABILITIES) != 0 ||
      !all_zero(service.reserved, sizeof(service.reserved) / sizeof(service.reserved[0]))) {
    ret = OB_INVALID_ARGUMENT;
    error = "invalid provided service descriptor";
  } else {
    uint32_t service_struct_size = 0;
    std::memcpy(&service_struct_size, service.service, sizeof(service_struct_size));
    if (service_struct_size < sizeof(service_struct_size)) {
      error = "provided service table has an invalid struct size";
      return OB_INVALID_DATA;
    }
    staged.spec_ = ObPluginServiceSpec(service.service_id, service.version.major,
                                       service.version.minor, service.version.patch,
                                       service.capabilities, service.service);
  }
  return ret;
}

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL host_acquire_service(
    seekdb_plugin_host_handle_t *opaque,
    const char *service_id,
    const seekdb_plugin_version_range_t *range,
    const seekdb_plugin_capability_t required_capabilities,
    const void **out_service,
    seekdb_plugin_semantic_version_t *out_version,
    seekdb_plugin_service_lease_t **out_lease)
{
  int ret = OB_SUCCESS;
  HostLease *holder = nullptr;
  try {
    HostContext *host = as_host(opaque);
    if (nullptr != out_service) *out_service = nullptr;
    if (nullptr != out_lease) *out_lease = nullptr;
    if (nullptr != out_version) std::memset(out_version, 0, sizeof(*out_version));
    if (nullptr == host || nullptr == host->registry_ || !valid_identifier(service_id) ||
        nullptr == range || !valid_range(*range) || range->minimum_inclusive.major == 0 ||
        (required_capabilities & ~KNOWN_RUNTIME_CAPABILITIES) != 0 ||
        nullptr == out_service || nullptr == out_version || nullptr == out_lease) {
      ret = OB_INVALID_ARGUMENT;
    } else if (nullptr == (holder = new (std::nothrow) HostLease(host))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_SUCCESS != (ret = host->registry_->acquire(
                   service_id, range->minimum_inclusive.major,
                   range->minimum_inclusive.minor, range->minimum_inclusive.patch,
                   required_capabilities, holder->lease_))) {
      delete holder;
      holder = nullptr;
    } else {
      seekdb_plugin_semantic_version_t actual = {
          range->minimum_inclusive.major, holder->lease_.service_minor(),
          holder->lease_.service_patch()};
      if (!version_in_range(actual, *range) ||
          (holder->lease_.service_capabilities() & required_capabilities) !=
              required_capabilities) {
        holder->lease_.reset();
        delete holder;
        holder = nullptr;
        ret = OB_ENTRY_NOT_EXIST;
      } else {
        std::lock_guard<std::mutex> guard(host->mutex_);
        host->leases_.insert(holder);
        *out_service = holder->lease_.service();
        *out_version = actual;
        *out_lease = reinterpret_cast<seekdb_plugin_service_lease_t *>(holder);
      }
    }
  } catch (const std::bad_alloc &) {
    if (nullptr != holder) delete holder;
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    if (nullptr != holder) delete holder;
    ret = OB_ERR_UNEXPECTED;
  }
  return to_plugin_status(ret);
}

void SEEKDB_PLUGIN_CALL host_release_service(seekdb_plugin_host_handle_t *opaque,
                                             seekdb_plugin_service_lease_t *lease)
{
  try {
    HostContext *host = as_host(opaque);
    HostLease *holder = reinterpret_cast<HostLease *>(lease);
    if (nullptr != host && nullptr != holder) {
      std::lock_guard<std::mutex> guard(host->mutex_);
      const auto it = host->leases_.find(holder);
      if (it != host->leases_.end()) {
        host->leases_.erase(it);
        delete holder;
      }
    }
  } catch (...) {
  }
}

seekdb_plugin_status_t registration_status(const int32_t status)
{
  switch (status) {
    case SEEKDB_RUNTIME_OK: return SEEKDB_PLUGIN_STATUS_OK;
    case SEEKDB_RUNTIME_INVALID: return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    case SEEKDB_RUNTIME_LIMIT: return SEEKDB_PLUGIN_STATUS_INVALID_MANIFEST;
    case SEEKDB_RUNTIME_STATE_MISMATCH: return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
    case SEEKDB_RUNTIME_NO_MEMORY: return SEEKDB_PLUGIN_STATUS_NO_MEMORY;
    case SEEKDB_RUNTIME_CONFLICT: return SEEKDB_PLUGIN_STATUS_ALREADY_EXISTS;
    default: return SEEKDB_PLUGIN_STATUS_INTERNAL;
  }
}

const seekdb_runtime_registration_token *registration_token(seekdb_plugin_registration_txn_t *token)
{
  return reinterpret_cast<const seekdb_runtime_registration_token *>(token);
}

void release_service_contribution(void *payload) { delete static_cast<StagedService *>(payload); }
void release_extension_contribution(void *payload) { delete static_cast<ObPluginExtensionSpec *>(payload); }

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL host_begin_registration(
    seekdb_plugin_host_handle_t *opaque, seekdb_plugin_registration_txn_t **out_txn)
{
  if (nullptr != out_txn) *out_txn = nullptr;
  HostContext *host = as_host(opaque);
  if (nullptr == host || nullptr == out_txn) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  try {
    std::lock_guard<std::mutex> guard(host->mutex_);
    seekdb_runtime_registration_token *token = nullptr;
    const int32_t status = seekdb_runtime_registration_begin(host->registration_.get(), &token);
    *out_txn = reinterpret_cast<seekdb_plugin_registration_txn_t *>(token);
    return registration_status(status);
  } catch (...) { return SEEKDB_PLUGIN_STATUS_INTERNAL; }
}

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL host_register_service(
    seekdb_plugin_host_handle_t *opaque, seekdb_plugin_registration_txn_t *token,
    const seekdb_plugin_service_provide_descriptor_t *service)
{
  HostContext *host = as_host(opaque);
  if (nullptr == host || nullptr == token || nullptr == service) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  try {
    std::lock_guard<std::mutex> guard(host->mutex_);
    const int32_t check = seekdb_runtime_registration_check(host->registration_.get(), registration_token(token));
    if (SEEKDB_RUNTIME_OK != check) return registration_status(check);
    std::unique_ptr<StagedService> staged(new StagedService());
    std::string error;
    const int ret = validate_registration_service(*service, *staged, error);
    if (OB_SUCCESS != ret) return to_plugin_status(ret);
    const auto &spec = staged->spec_;
    const int32_t status = seekdb_runtime_registration_stage(host->registration_.get(),
        registration_token(token), SEEKDB_RUNTIME_SERVICE, spec.abi_major_,
        reinterpret_cast<const uint8_t *>(spec.name_.data()), static_cast<uint32_t>(spec.name_.size()),
        0, staged.get(), release_service_contribution);
    if (SEEKDB_RUNTIME_OK == status) (void)staged.release();
    return registration_status(status);
  } catch (const std::bad_alloc &) { return SEEKDB_PLUGIN_STATUS_NO_MEMORY; }
  catch (...) { return SEEKDB_PLUGIN_STATUS_INTERNAL; }
}

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL host_commit_registration(
    seekdb_plugin_host_handle_t *opaque, seekdb_plugin_registration_txn_t *token)
{
  HostContext *host = as_host(opaque);
  if (nullptr == host || nullptr == token) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  try {
    std::lock_guard<std::mutex> guard(host->mutex_);
    return registration_status(seekdb_runtime_registration_commit(host->registration_.get(), registration_token(token)));
  } catch (...) { return SEEKDB_PLUGIN_STATUS_INTERNAL; }
}

void SEEKDB_PLUGIN_CALL host_abort_registration(
    seekdb_plugin_host_handle_t *opaque, seekdb_plugin_registration_txn_t *token)
{
  HostContext *host = as_host(opaque);
  if (nullptr == host || nullptr == token) return;
  try {
    std::lock_guard<std::mutex> guard(host->mutex_);
    (void)seekdb_runtime_registration_abort(host->registration_.get(), registration_token(token));
  } catch (...) {}
}

int collect_registered_objects(HostContext &host)
{
  seekdb_runtime_registration_stats_t stats = {};
  if (SEEKDB_RUNTIME_OK != seekdb_runtime_registration_stats(host.registration_.get(), &stats)) {
    return OB_ERR_UNEXPECTED;
  }
  std::vector<StagedService> services;
  std::vector<ObPluginExtensionSpec> extensions;
  services.reserve(stats.committed_services);
  extensions.reserve(stats.committed_extensions);
  for (uint32_t i = 0; i < stats.committed_services + stats.committed_extensions; ++i) {
    uint32_t family = 0;
    const void *payload = nullptr;
    if (SEEKDB_RUNTIME_OK != seekdb_runtime_registration_get(host.registration_.get(), i, &family, &payload) ||
        nullptr == payload) return OB_ERR_UNEXPECTED;
    if (SEEKDB_RUNTIME_SERVICE == family) services.push_back(*static_cast<const StagedService *>(payload));
    else if (SEEKDB_RUNTIME_EXTENSION == family) extensions.push_back(*static_cast<const ObPluginExtensionSpec *>(payload));
    else return OB_ERR_UNEXPECTED;
  }
  host.staged_.swap(services);
  host.staged_extensions_.swap(extensions);
  return OB_SUCCESS;
}

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL host_register_extension(
    seekdb_plugin_host_handle_t *opaque,
    seekdb_plugin_registration_txn_t *opaque_txn,
    seekdb_plugin_extension_kind_t kind,
    const void *descriptor,
    uint32_t descriptor_bytes);

void init_host_api(HostContext &host)
{
  std::memset(&host.api_, 0, sizeof(host.api_));
  auto &v2 = host.api_.v2;
  v2.host.struct_size = sizeof(host.api_);
  v2.host.abi_major = SEEKDB_PLUGIN_ABI_MAJOR;
  v2.host.abi_minor = SEEKDB_PLUGIN_ABI_MINOR;
  v2.host.host_handle = reinterpret_cast<seekdb_plugin_host_handle_t *>(&host);
  v2.host.alloc = host_alloc;
  v2.host.free = host_free;
  v2.host.log = host_log;
  v2.host.acquire_service = host_acquire_service;
  v2.host.release_service = host_release_service;
  v2.host.begin_registration = host_begin_registration;
  v2.host.register_service = host_register_service;
  v2.host.commit_registration = host_commit_registration;
  v2.host.abort_registration = host_abort_registration;
  v2.registration_spi_major = SEEKDB_PLUGIN_REGISTRATION_SPI_MAJOR;
  v2.register_extension = host_register_extension;
  host.api_.memory_spi_major = 1;
  host.api_.allocate_owned_bytes = host_allocate_owned_bytes;
}

void cleanup_host_resources(HostContext &host)
{
  std::lock_guard<std::mutex> guard(host.mutex_);
  seekdb_runtime_memory_close(host.memory_.get());
  (void)seekdb_runtime_registration_clear(host.registration_.get());
  for (HostLease *lease : host.leases_) delete lease;
  host.leases_.clear();
  host.staged_.clear();
  host.staged_extensions_.clear();
}

typedef seekdb_runtime_native_module *ModuleHandle;
const ModuleHandle INVALID_MODULE = nullptr;

#if defined(_WIN32)
std::string windows_error(const DWORD code)
{
  char *buffer = nullptr;
  const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
      FORMAT_MESSAGE_IGNORE_INSERTS;
  FormatMessageA(flags, nullptr, code, 0, reinterpret_cast<char *>(&buffer), 0, nullptr);
  std::string message = nullptr == buffer ? "Windows loader error" : buffer;
  if (nullptr != buffer) LocalFree(buffer);
  return message;
}

int canonical_existing(const std::string &path, const bool directory,
                       std::string &canonical, std::string &error)
{
  int ret = OB_SUCCESS;
  HANDLE file = CreateFileA(path.c_str(), 0,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING,
                            directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (INVALID_HANDLE_VALUE == file) {
    ret = OB_FILE_NOT_EXIST;
    error = windows_error(GetLastError());
  } else {
    const DWORD attrs = GetFileAttributesA(path.c_str());
    if (INVALID_FILE_ATTRIBUTES == attrs ||
        (directory != ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0))) {
      ret = OB_INVALID_ARGUMENT;
      error = directory ? "trusted plugin path is not a directory" :
                          "plugin path is not a regular file";
    } else {
      const DWORD needed = GetFinalPathNameByHandleA(file, nullptr, 0,
                                                     FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
      std::vector<char> value(needed + 1, '\0');
      const DWORD copied = GetFinalPathNameByHandleA(file, value.data(),
                                                     static_cast<DWORD>(value.size()),
                                                     FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
      if (copied == 0 || copied >= value.size()) {
        ret = OB_IO_ERROR;
        error = windows_error(GetLastError());
      } else {
        canonical.assign(value.data(), copied);
        const std::string prefix = "\\\\?\\";
        if (canonical.compare(0, prefix.size(), prefix) == 0) canonical.erase(0, prefix.size());
        std::replace(canonical.begin(), canonical.end(), '/', '\\');
        std::transform(canonical.begin(), canonical.end(), canonical.begin(),
                       [](const char c) { return static_cast<char>(
                           std::tolower(static_cast<unsigned char>(c))); });
      }
    }
    CloseHandle(file);
  }
  return ret;
}

#else

int canonical_existing(const std::string &path, const bool directory,
                       std::string &canonical, std::string &error)
{
  int ret = OB_SUCCESS;
  char *resolved = realpath(path.c_str(), nullptr);
  if (nullptr == resolved) {
    ret = OB_FILE_NOT_EXIST;
    error = std::strerror(errno);
  } else {
    canonical.assign(resolved);
    std::free(resolved);
    struct stat info;
    if (0 != stat(canonical.c_str(), &info)) {
      ret = OB_IO_ERROR;
      error = std::strerror(errno);
    } else if ((directory && !S_ISDIR(info.st_mode)) ||
               (!directory && !S_ISREG(info.st_mode))) {
      ret = OB_INVALID_ARGUMENT;
      error = directory ? "trusted plugin path is not a directory" :
                          "plugin path is not a regular file";
    }
  }
  return ret;
}
#endif

ModuleHandle open_module(const std::string &path, std::string &error)
{
  ModuleHandle module = INVALID_MODULE;
  char diagnostic[1024] = {};
  if (path.size() > UINT32_MAX) {
    error = "native module path is too long";
  } else if (SEEKDB_RUNTIME_OK != seekdb_runtime_native_open(
      reinterpret_cast<const uint8_t *>(path.data()),
      static_cast<uint32_t>(path.size()), &module, diagnostic, sizeof(diagnostic))) {
    assign_error_noexcept(error, diagnostic);
  }
  return module;
}

int close_module(ModuleHandle module, uint32_t phase, std::string &error)
{
  char diagnostic[1024] = {};
  const int32_t status = seekdb_runtime_native_close(
      module, phase, diagnostic, sizeof(diagnostic));
  if (SEEKDB_RUNTIME_OK != status) {
    assign_error_noexcept(error, diagnostic);
    return SEEKDB_RUNTIME_STATE_MISMATCH == status ? OB_STATE_NOT_MATCH : OB_IO_ERROR;
  }
  return OB_SUCCESS;
}

int find_entry(ModuleHandle module, seekdb_plugin_entry_v1_fn &entry, std::string &error)
{
  entry = nullptr;
  seekdb_runtime_native_entry_fn symbol = nullptr;
  char diagnostic[1024] = {};
  if (SEEKDB_RUNTIME_OK != seekdb_runtime_native_entry(
      module, &symbol, diagnostic, sizeof(diagnostic))) {
    assign_error_noexcept(error, diagnostic);
    return OB_ENTRY_NOT_EXIST;
  }
  if (sizeof(symbol) != sizeof(entry)) {
    error = "platform function pointer representation is unsupported";
    return OB_NOT_SUPPORTED;
  }
  std::memcpy(&entry, &symbol, sizeof(entry));
  return OB_SUCCESS;
}

bool contains_path(const std::string &directory, const std::string &path)
{
  if (path.size() <= directory.size() || path.compare(0, directory.size(), directory) != 0) {
    return false;
  }
  const char separator = path[directory.size()];
  return separator == '/' || separator == '\\';
}

bool safe_relative_path(const std::string &path)
{
  if (path.empty() || path[0] == '/' || path[0] == '\\' ||
      (path.size() >= 2 && path[1] == ':') || path.find('\0') != std::string::npos) {
    return false;
  }
  size_t start = 0;
  while (start <= path.size()) {
    const size_t end = path.find_first_of("/\\", start);
    const std::string component = path.substr(start, end - start);
    if (component.empty() || component == "." || component == "..") return false;
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return true;
}

int call_lifecycle_init(const seekdb_plugin_init_fn fn,
                        const seekdb_plugin_host_api_v1_t *api,
                        seekdb_plugin_instance_handle_t **instance)
{
  int ret = OB_ERR_UNEXPECTED;
  try {
    ret = from_plugin_status(fn(api, instance));
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int call_lifecycle(const seekdb_plugin_start_fn fn,
                   seekdb_plugin_instance_handle_t *instance)
{
  int ret = OB_ERR_UNEXPECTED;
  try {
    ret = from_plugin_status(fn(instance));
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

void call_deinit(const seekdb_plugin_deinit_fn fn,
                 seekdb_plugin_instance_handle_t *instance)
{
  try {
    fn(instance);
  } catch (...) {
  }
}

bool valid_sql_name(const char *value, const bool allow_qualified = true)
{
  size_t length = 0;
  bool valid = bounded_string(value, MAX_PLUGIN_STRING, length);
  for (size_t i = 0; valid && i < length; ++i) {
    const char c = value[i];
    if (c == '.') {
      valid = allow_qualified && i != 0 && i + 1 < length &&
          value[i - 1] != '.';
    } else {
      valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
          c == '_' || c == '$';
    }
  }
  return valid;
}

bool valid_digest(const char *value)
{
  static const char PREFIX[] = "sha256:";
  size_t length = 0;
  bool valid = bounded_string(value, MAX_PLUGIN_STRING, length) &&
      length == sizeof(PREFIX) - 1 + 64 &&
      0 == std::memcmp(value, PREFIX, sizeof(PREFIX) - 1);
  for (size_t i = sizeof(PREFIX) - 1; valid && i < length; ++i) {
    const char c = value[i];
    valid = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  }
  return valid;
}

bool valid_digest(const std::string &value)
{
  static const char PREFIX[] = "sha256:";
  bool valid = value.size() == sizeof(PREFIX) - 1 + 64 &&
      0 == value.compare(0, sizeof(PREFIX) - 1, PREFIX);
  for (size_t i = sizeof(PREFIX) - 1; valid && i < value.size(); ++i) {
    const char c = value[i];
    valid = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  }
  return valid;
}

int normalize_implementation(
    const seekdb_plugin_implementation_ref_v1_t &source,
    ObPluginImplementationSpec &target,
    std::string &error)
{
  int ret = OB_SUCCESS;
  if (source.struct_size != sizeof(seekdb_plugin_implementation_ref_v1_t) ||
      !valid_identifier(source.service_id) ||
      !valid_range(source.version_range) ||
      0 == source.version_range.minimum_inclusive.major ||
      (source.required_capabilities & ~KNOWN_RUNTIME_CAPABILITIES) != 0 ||
      !all_zero(source.reserved,
                sizeof(source.reserved) / sizeof(source.reserved[0]))) {
    ret = OB_INVALID_DATA;
    error = "invalid extension implementation service reference";
  } else {
    target.service_id_ = source.service_id;
    target.version_range_ = source.version_range;
    // Keep only the v1 fields in the host-owned normalized representation.
    target.version_range_.struct_size = sizeof(target.version_range_);
    std::memset(target.version_range_.reserved, 0,
                sizeof(target.version_range_.reserved));
    target.required_capabilities_ = source.required_capabilities;
  }
  return ret;
}

int validate_extension_common(const uint32_t struct_size,
                              const uint32_t required_size,
                              const char *object_id,
                              const seekdb_plugin_extension_flags_t flags,
                              const uint64_t *reserved,
                              const size_t reserved_count,
                              const char *kind,
                              std::string &error)
{
  int ret = OB_SUCCESS;
  if (struct_size < required_size || !valid_identifier(object_id) ||
      (flags & ~KNOWN_EXTENSION_FLAGS) != 0 ||
      !all_zero(reserved, reserved_count)) {
    ret = OB_INVALID_DATA;
    error = std::string("invalid ") + kind + " extension descriptor";
  }
  return ret;
}

int normalize_extension(const seekdb_plugin_type_descriptor_v1_t &source,
                        ObPluginExtensionSpec &target,
                        std::string &error)
{
  int ret = validate_extension_common(
      source.struct_size, sizeof(source), source.object_id, source.flags,
      source.reserved, sizeof(source.reserved) / sizeof(source.reserved[0]),
      "type", error);
  if (OB_SUCCESS == ret &&
      (!valid_sql_name(source.sql_name) ||
       !valid_identifier(source.physical_format_id) ||
       0 == source.physical_format_version || 0 != source.reserved_word)) {
    ret = OB_INVALID_DATA;
    error = "invalid type extension metadata";
  }
  if (OB_SUCCESS == ret) {
    target.kind_ = SEEKDB_PLUGIN_EXTENSION_TYPE;
    target.object_id_ = source.object_id;
    target.sql_name_ = source.sql_name;
    target.physical_format_id_ = source.physical_format_id;
    target.physical_format_version_ = source.physical_format_version;
    target.flags_ = source.flags;
    ret = normalize_implementation(source.codec_service,
                                   target.implementation_, error);
  }
  return ret;
}

int normalize_extension(const seekdb_plugin_function_descriptor_v1_t &source,
                        ObPluginExtensionSpec &target,
                        std::string &error)
{
  int ret = validate_extension_common(
      source.struct_size, sizeof(source), source.object_id, source.flags,
      source.reserved, sizeof(source.reserved) / sizeof(source.reserved[0]),
      "function", error);
  if (OB_SUCCESS == ret &&
      (!valid_sql_name(source.sql_name) ||
       source.minimum_arity > source.maximum_arity ||
       (nullptr != source.static_result_type_id &&
        !valid_identifier(source.static_result_type_id)))) {
    ret = OB_INVALID_DATA;
    error = "invalid function extension metadata";
  }
  if (OB_SUCCESS == ret) {
    target.kind_ = SEEKDB_PLUGIN_EXTENSION_FUNCTION;
    target.object_id_ = source.object_id;
    target.sql_name_ = source.sql_name;
    target.minimum_arity_ = source.minimum_arity;
    target.maximum_arity_ = source.maximum_arity;
    if (nullptr != source.static_result_type_id) {
      target.static_result_type_id_ = source.static_result_type_id;
    }
    target.flags_ = source.flags;
    ret = normalize_implementation(source.implementation,
                                   target.implementation_, error);
  }
  return ret;
}

int normalize_typed_signature(const char *const *argument_type_ids,
                              const uint32_t argument_type_count,
                              const uint32_t signature_flags,
                              const uint32_t minimum_arity,
                              const uint32_t maximum_arity,
                              ObPluginExtensionSpec &target,
                              std::string &error)
{
  if (maximum_arity > SEEKDB_PLUGIN_MAX_ARGUMENTS ||
      argument_type_count > maximum_arity ||
      (argument_type_count != 0 && nullptr == argument_type_ids) ||
      (signature_flags & ~SEEKDB_PLUGIN_SIGNATURE_FLAG_VARIADIC) != 0 ||
      (signature_flags != 0 && argument_type_count == 0) ||
      (argument_type_count != 0 &&
       (signature_flags & SEEKDB_PLUGIN_SIGNATURE_FLAG_VARIADIC) == 0 &&
       argument_type_count != maximum_arity) ||
      (argument_type_count != 0 && argument_type_count < minimum_arity &&
       (signature_flags & SEEKDB_PLUGIN_SIGNATURE_FLAG_VARIADIC) == 0)) {
    error = "invalid typed SQL extension signature";
    return OB_INVALID_DATA;
  }

  target.signature_flags_ = signature_flags;
  for (uint32_t i = 0; i < argument_type_count; ++i) {
    if (!valid_identifier(argument_type_ids[i])) {
      error = "invalid typed SQL extension argument type";
      return OB_INVALID_DATA;
    }
    target.argument_type_ids_.push_back(argument_type_ids[i]);
  }
  return OB_SUCCESS;
}

int normalize_extension(
    const seekdb_plugin_table_function_descriptor_v1_t &source,
    ObPluginExtensionSpec &target,
    std::string &error)
{
  int ret = validate_extension_common(
      source.struct_size, sizeof(source), source.object_id, source.flags,
      source.reserved, sizeof(source.reserved) / sizeof(source.reserved[0]),
      "table function", error);
  if (OB_SUCCESS == ret &&
      (!valid_sql_name(source.sql_name) ||
       source.minimum_arity > source.maximum_arity ||
       source.column_count == 0 ||
       source.column_count > SEEKDB_PLUGIN_MAX_ARGUMENTS ||
       nullptr == source.columns || source.reserved_word != 0)) {
    error = "invalid table function extension metadata";
    ret = OB_INVALID_DATA;
  }
  if (OB_SUCCESS == ret) {
    target.kind_ = SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION;
    target.object_id_ = source.object_id;
    target.sql_name_ = source.sql_name;
    target.minimum_arity_ = source.minimum_arity;
    target.maximum_arity_ = source.maximum_arity;
    target.flags_ = source.flags;
    ret = normalize_typed_signature(
        source.argument_type_ids, source.argument_type_count,
        source.signature_flags, source.minimum_arity, source.maximum_arity,
        target, error);
  }
  for (uint32_t i = 0; OB_SUCCESS == ret && i < source.column_count; ++i) {
    const seekdb_plugin_table_column_descriptor_v1_t &column = source.columns[i];
    if (column.struct_size != sizeof(column) ||
        !valid_sql_name(column.sql_name, false) ||
        !valid_identifier(column.type_id) || column.nullable > 1 ||
        !all_zero(column.reserved, sizeof(column.reserved) /
                                     sizeof(column.reserved[0])) ||
        !std::all_of(column.reserved_bytes,
                     column.reserved_bytes + sizeof(column.reserved_bytes),
                     [](const uint8_t value) { return value == 0; })) {
      error = "invalid table function result column";
      ret = OB_INVALID_DATA;
    } else {
      PluginSqlColumn normalized;
      normalized.sql_name_ = column.sql_name;
      normalized.type_id_ = column.type_id;
      normalized.nullable_ = column.nullable != 0;
      target.result_columns_.push_back(normalized);
    }
  }
  if (OB_SUCCESS == ret) {
    ret = normalize_implementation(source.implementation,
                                   target.implementation_, error);
  }
  return ret;
}

int normalize_extension(const seekdb_plugin_cast_descriptor_v1_t &source,
                        ObPluginExtensionSpec &target,
                        std::string &error)
{
  int ret = validate_extension_common(
      source.struct_size, sizeof(source), source.object_id, source.flags,
      source.reserved, sizeof(source.reserved) / sizeof(source.reserved[0]),
      "cast", error);
  if (OB_SUCCESS == ret &&
      (!valid_identifier(source.source_type_id) ||
       !valid_identifier(source.target_type_id) ||
       source.context < SEEKDB_PLUGIN_CAST_EXPLICIT ||
       source.context > SEEKDB_PLUGIN_CAST_IMPLICIT)) {
    ret = OB_INVALID_DATA;
    error = "invalid cast extension metadata";
  }
  if (OB_SUCCESS == ret) {
    target.kind_ = SEEKDB_PLUGIN_EXTENSION_CAST;
    target.object_id_ = source.object_id;
    target.source_type_id_ = source.source_type_id;
    target.target_type_id_ = source.target_type_id;
    target.cast_context_ = source.context;
    target.cost_ = source.cost;
    target.flags_ = source.flags;
    ret = normalize_implementation(source.implementation,
                                   target.implementation_, error);
  }
  return ret;
}

int normalize_extension(
    const seekdb_plugin_index_access_method_descriptor_v1_t &source,
    ObPluginExtensionSpec &target,
    std::string &error)
{
  int ret = validate_extension_common(
      source.struct_size, sizeof(source), source.object_id, source.flags,
      source.reserved, sizeof(source.reserved) / sizeof(source.reserved[0]),
      "index access method", error);
  if (OB_SUCCESS == ret && !valid_sql_name(source.sql_name)) {
    ret = OB_INVALID_DATA;
    error = "invalid index access method extension metadata";
  }
  if (OB_SUCCESS == ret) {
    target.kind_ = SEEKDB_PLUGIN_EXTENSION_INDEX_ACCESS_METHOD;
    target.object_id_ = source.object_id;
    target.sql_name_ = source.sql_name;
    target.flags_ = source.flags;
    ret = normalize_implementation(source.implementation,
                                   target.implementation_, error);
  }
  return ret;
}

int normalize_extension(const seekdb_plugin_optimizer_hook_descriptor_v1_t &source,
                        ObPluginExtensionSpec &target,
                        std::string &error)
{
  int ret = validate_extension_common(
      source.struct_size, sizeof(source), source.object_id, source.flags,
      source.reserved, sizeof(source.reserved) / sizeof(source.reserved[0]),
      "optimizer hook", error);
  if (OB_SUCCESS == ret &&
      (!valid_identifier(source.hook_point) || 0 != source.reserved_word)) {
    ret = OB_INVALID_DATA;
    error = "invalid optimizer hook extension metadata";
  }
  if (OB_SUCCESS == ret) {
    target.kind_ = SEEKDB_PLUGIN_EXTENSION_OPTIMIZER_HOOK;
    target.object_id_ = source.object_id;
    target.hook_point_ = source.hook_point;
    target.priority_ = source.priority;
    target.flags_ = source.flags;
    ret = normalize_implementation(source.implementation,
                                   target.implementation_, error);
  }
  return ret;
}

int normalize_extension(const seekdb_plugin_das_hook_descriptor_v1_t &source,
                        ObPluginExtensionSpec &target,
                        std::string &error)
{
  int ret = validate_extension_common(
      source.struct_size, sizeof(source), source.object_id, source.flags,
      source.reserved, sizeof(source.reserved) / sizeof(source.reserved[0]),
      "DAS hook", error);
  if (OB_SUCCESS == ret &&
      (!valid_identifier(source.hook_point) || 0 != source.reserved_word)) {
    ret = OB_INVALID_DATA;
    error = "invalid DAS hook extension metadata";
  }
  if (OB_SUCCESS == ret) {
    target.kind_ = SEEKDB_PLUGIN_EXTENSION_DAS_HOOK;
    target.object_id_ = source.object_id;
    target.hook_point_ = source.hook_point;
    target.priority_ = source.priority;
    target.flags_ = source.flags;
    ret = normalize_implementation(source.implementation,
                                   target.implementation_, error);
  }
  return ret;
}

int normalize_extension(
    const seekdb_plugin_catalog_object_descriptor_v1_t &source,
    ObPluginExtensionSpec &target,
    std::string &error)
{
  int ret = validate_extension_common(
      source.struct_size, sizeof(source), source.object_id, source.flags,
      source.reserved, sizeof(source.reserved) / sizeof(source.reserved[0]),
      "catalog object", error);
  const seekdb_plugin_extension_flags_t required_flags =
      SEEKDB_PLUGIN_EXTENSION_FLAG_PERSISTENT |
      SEEKDB_PLUGIN_EXTENSION_FLAG_REQUIRES_CATALOG;
  if (OB_SUCCESS == ret &&
      (!valid_identifier(source.object_kind) ||
       !valid_sql_name(source.schema_name, false) ||
       !valid_sql_name(source.sql_name, false) ||
       !valid_digest(source.definition_digest) ||
       required_flags != (source.flags & required_flags))) {
    ret = OB_INVALID_DATA;
    error = "invalid catalog object extension metadata";
  }
  if (OB_SUCCESS == ret) {
    target.kind_ = SEEKDB_PLUGIN_EXTENSION_CATALOG_OBJECT;
    target.object_id_ = source.object_id;
    target.catalog_object_kind_ = source.object_kind;
    target.schema_name_ = source.schema_name;
    target.sql_name_ = source.sql_name;
    target.definition_digest_ = source.definition_digest;
    target.flags_ = source.flags;
  }
  return ret;
}

struct ExtensionManifestRequirements
{
  ExtensionManifestRequirements()
      : requires_catalog_(false), persistent_(false),
        persistent_data_format_(false)
  {}

  void observe(const ObPluginExtensionSpec &extension)
  {
    requires_catalog_ = requires_catalog_ ||
        0 != (extension.flags_ &
              SEEKDB_PLUGIN_EXTENSION_FLAG_REQUIRES_CATALOG);
    const bool persistent =
        0 != (extension.flags_ & SEEKDB_PLUGIN_EXTENSION_FLAG_PERSISTENT);
    persistent_ = persistent_ || persistent;
    persistent_data_format_ = persistent_data_format_ ||
        (persistent &&
         (SEEKDB_PLUGIN_EXTENSION_TYPE == extension.kind_ ||
          SEEKDB_PLUGIN_EXTENSION_INDEX_ACCESS_METHOD == extension.kind_));
  }

  bool requires_catalog_;
  bool persistent_;
  bool persistent_data_format_;
};

template <typename Descriptor>
int normalize_descriptor_suffix(const Descriptor &,
                                const unsigned char *,
                                const uint32_t,
                                ObPluginExtensionSpec &,
                                std::string &)
{
  return OB_SUCCESS;
}

int normalize_descriptor_suffix(
    const seekdb_plugin_function_descriptor_v1_t &descriptor,
    const unsigned char *bytes,
    const uint32_t descriptor_size,
    ObPluginExtensionSpec &target,
    std::string &error)
{
  if (descriptor_size < sizeof(seekdb_plugin_function_descriptor_v2_t)) {
    return OB_SUCCESS;
  }
  seekdb_plugin_function_descriptor_v2_t typed;
  std::memcpy(&typed, bytes, sizeof(typed));
  if (!all_zero(typed.signature_reserved,
                sizeof(typed.signature_reserved) /
                    sizeof(typed.signature_reserved[0]))) {
    error = "invalid typed SQL function reserved fields";
    return OB_INVALID_DATA;
  }
  return normalize_typed_signature(
      typed.argument_type_ids, typed.argument_type_count,
      typed.signature_flags, descriptor.minimum_arity,
      descriptor.maximum_arity, target, error);
}

// Normalize a single borrowed descriptor through exactly the same checks as
// snapshot discovery. Copy the prefix first: callers need not align the input.
template <typename Descriptor>
int normalize_registered_descriptor(const void *data, uint32_t bytes,
                                    ObPluginExtensionSpec &target,
                                    std::string &error)
{
  if (bytes < sizeof(Descriptor)) return OB_INVALID_DATA;
  Descriptor descriptor;
  std::memcpy(&descriptor, data, sizeof(descriptor));
  int ret = normalize_extension(descriptor, target, error);
  if (OB_SUCCESS == ret) {
    ret = normalize_descriptor_suffix(descriptor,
        static_cast<const unsigned char *>(data), bytes, target, error);
  }
  return ret;
}

int normalize_registered_extension(seekdb_plugin_extension_kind_t kind,
                                  const void *data, uint32_t bytes,
                                  ObPluginExtensionSpec &target,
                                  std::string &error)
{
  if (nullptr == data || bytes < sizeof(uint32_t) ||
      bytes > SEEKDB_PLUGIN_MAX_EXTENSION_DESCRIPTOR_BYTES) return OB_INVALID_ARGUMENT;
  uint32_t size = 0;
  std::memcpy(&size, data, sizeof(size));
  if (size != bytes) return OB_INVALID_DATA;
  switch (kind) {
    case SEEKDB_PLUGIN_EXTENSION_TYPE:
      return normalize_registered_descriptor<seekdb_plugin_type_descriptor_v1_t>(data, bytes, target, error);
    case SEEKDB_PLUGIN_EXTENSION_FUNCTION:
      return normalize_registered_descriptor<seekdb_plugin_function_descriptor_v1_t>(data, bytes, target, error);
    case SEEKDB_PLUGIN_EXTENSION_CAST:
      return normalize_registered_descriptor<seekdb_plugin_cast_descriptor_v1_t>(data, bytes, target, error);
    case SEEKDB_PLUGIN_EXTENSION_INDEX_ACCESS_METHOD:
      return normalize_registered_descriptor<seekdb_plugin_index_access_method_descriptor_v1_t>(data, bytes, target, error);
    case SEEKDB_PLUGIN_EXTENSION_OPTIMIZER_HOOK:
      return normalize_registered_descriptor<seekdb_plugin_optimizer_hook_descriptor_v1_t>(data, bytes, target, error);
    case SEEKDB_PLUGIN_EXTENSION_DAS_HOOK:
      return normalize_registered_descriptor<seekdb_plugin_das_hook_descriptor_v1_t>(data, bytes, target, error);
    case SEEKDB_PLUGIN_EXTENSION_CATALOG_OBJECT:
      return normalize_registered_descriptor<seekdb_plugin_catalog_object_descriptor_v1_t>(data, bytes, target, error);
    case SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION:
      return normalize_registered_descriptor<seekdb_plugin_table_function_descriptor_v1_t>(data, bytes, target, error);
    default: return OB_NOT_SUPPORTED;
  }
}

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL host_register_extension(
    seekdb_plugin_host_handle_t *opaque,
    seekdb_plugin_registration_txn_t *token,
    seekdb_plugin_extension_kind_t kind,
    const void *descriptor,
    uint32_t descriptor_bytes)
{
  HostContext *host = as_host(opaque);
  if (nullptr == host || nullptr == token || nullptr == descriptor) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  try {
    std::lock_guard<std::mutex> guard(host->mutex_);
    const int32_t check = seekdb_runtime_registration_check(host->registration_.get(), registration_token(token));
    if (SEEKDB_RUNTIME_OK != check) return registration_status(check);
    std::unique_ptr<ObPluginExtensionSpec> normalized(new ObPluginExtensionSpec());
    std::string error;
    const int ret = normalize_registered_extension(kind, descriptor, descriptor_bytes, *normalized, error);
    if (OB_SUCCESS != ret) return to_plugin_status(ret);
    const std::string &key = normalized->object_id_;
    const int32_t status = seekdb_runtime_registration_stage(host->registration_.get(),
        registration_token(token), SEEKDB_RUNTIME_EXTENSION, 0,
        reinterpret_cast<const uint8_t *>(key.data()), static_cast<uint32_t>(key.size()),
        descriptor_bytes, normalized.get(), release_extension_contribution);
    if (SEEKDB_RUNTIME_OK == status) (void)normalized.release();
    return registration_status(status);
  } catch (const std::bad_alloc &) { return SEEKDB_PLUGIN_STATUS_NO_MEMORY; }
  catch (...) { return SEEKDB_PLUGIN_STATUS_INTERNAL; }
}

int validate_extension_requirements(const ExtensionManifestRequirements &requirements,
                                    const seekdb_plugin_manifest_v1_t &manifest,
                                    std::string &error)
{
  if (requirements.requires_catalog_ && 0 == manifest.catalog_version) {
    error = "extension metadata requires a nonzero catalog version";
  } else if (requirements.persistent_ &&
             0 == (manifest.capabilities & SEEKDB_PLUGIN_CAPABILITY_PERSISTENT_DATA)) {
    error = "persistent extension metadata requires plugin persistent-data capability";
  } else if (requirements.persistent_data_format_ && 0 == manifest.data_format_version) {
    error = "persistent type or index metadata requires a data format version";
  } else {
    return OB_SUCCESS;
  }
  return OB_INVALID_DATA;
}

template <typename Descriptor>
int stage_extension_array(
    const Descriptor *descriptors,
    const uint32_t count,
    const uint32_t array_bytes,
    ObPluginRegistration &publication,
    ExtensionManifestRequirements &requirements,
    std::string &error,
    const char *kind,
    int (*normalize)(const Descriptor &, ObPluginExtensionSpec &, std::string &))
{
  int ret = OB_SUCCESS;
  size_t offset = 0;
  const unsigned char *bytes =
      reinterpret_cast<const unsigned char *>(descriptors);
  for (uint32_t i = 0; OB_SUCCESS == ret && i < count; ++i) {
    uint32_t descriptor_size = 0;
    if (offset > array_bytes ||
        array_bytes - offset < sizeof(descriptor_size)) {
      ret = OB_INVALID_DATA;
      error = std::string("truncated ") + kind + " extension array";
    } else {
      std::memcpy(&descriptor_size, bytes + offset, sizeof(descriptor_size));
    }
    if (OB_SUCCESS == ret &&
        (descriptor_size < sizeof(Descriptor) ||
        descriptor_size > SEEKDB_PLUGIN_MAX_EXTENSION_DESCRIPTOR_BYTES ||
        offset > std::numeric_limits<size_t>::max() - descriptor_size ||
        descriptor_size > array_bytes - offset)) {
      ret = OB_INVALID_DATA;
      error = std::string("invalid ") + kind + " extension array layout";
    } else if (OB_SUCCESS == ret) {
      Descriptor descriptor;
      std::memcpy(&descriptor, bytes + offset, sizeof(descriptor));
      ObPluginExtensionSpec normalized;
      ret = normalize(descriptor, normalized, error);
      if (OB_SUCCESS == ret) {
        ret = normalize_descriptor_suffix(
            descriptor, bytes + offset, descriptor_size, normalized, error);
      }
      if (OB_SUCCESS == ret) {
        requirements.observe(normalized);
        ret = publication.add_extension(normalized);
        if (OB_SUCCESS != ret) {
          error = std::string("cannot stage ") + kind +
              " extension: " + normalized.object_id_;
        }
      }
      offset += descriptor_size;
    }
  }
  if (OB_SUCCESS == ret && offset != array_bytes) {
    ret = OB_INVALID_DATA;
    error = std::string("invalid trailing bytes in ") + kind +
        " extension array";
  }
  return ret;
}

bool valid_extension_array_span(const void *data,
                                const uint32_t count,
                                const uint32_t bytes)
{
  return (0 == count && nullptr == data && 0 == bytes) ||
         (0 != count && nullptr != data &&
          static_cast<uint64_t>(count) * sizeof(uint32_t) <= bytes);
}

int validate_and_stage_extensions(
    const seekdb_plugin_extension_snapshot_v1_t *borrowed_snapshot,
    const seekdb_plugin_manifest_v1_t &manifest,
    ObPluginRegistration &publication,
    std::string &error)
{
  int ret = OB_SUCCESS;
  uint32_t snapshot_size = 0;
  if (nullptr == borrowed_snapshot) {
    ret = OB_INVALID_DATA;
    error = "extension catalog returned a null snapshot";
  } else {
    std::memcpy(&snapshot_size, borrowed_snapshot, sizeof(snapshot_size));
    if (snapshot_size < sizeof(seekdb_plugin_extension_snapshot_v1_t)) {
      ret = OB_INVALID_DATA;
      error = "extension snapshot has an invalid struct size";
    }
  }

  seekdb_plugin_extension_snapshot_v1_t snapshot;
  seekdb_plugin_extension_snapshot_v2_t extended_snapshot = {};
  bool has_table_functions = false;
  ExtensionManifestRequirements requirements;
  if (OB_SUCCESS == ret) {
    std::memcpy(&snapshot, borrowed_snapshot, sizeof(snapshot));
    if (snapshot_size >= sizeof(extended_snapshot)) {
      std::memcpy(&extended_snapshot, borrowed_snapshot,
                  sizeof(extended_snapshot));
      has_table_functions = true;
    }
    const uint64_t total = static_cast<uint64_t>(snapshot.type_count) +
        snapshot.function_count + snapshot.cast_count +
        snapshot.index_access_method_count + snapshot.optimizer_hook_count +
        snapshot.das_hook_count + snapshot.catalog_object_count +
        (has_table_functions ? extended_snapshot.table_function_count : 0);
    const uint64_t total_bytes = static_cast<uint64_t>(snapshot.type_bytes) +
        snapshot.function_bytes + snapshot.cast_bytes +
        snapshot.index_access_method_bytes + snapshot.optimizer_hook_bytes +
        snapshot.das_hook_bytes + snapshot.catalog_object_bytes +
        (has_table_functions ? extended_snapshot.table_function_bytes : 0);
    if (total > MAX_EXTENSION_COUNT ||
        total_bytes > SEEKDB_PLUGIN_MAX_EXTENSION_ARRAY_BYTES ||
        !valid_extension_array_span(
            snapshot.types, snapshot.type_count, snapshot.type_bytes) ||
        !valid_extension_array_span(snapshot.functions,
                                    snapshot.function_count,
                                    snapshot.function_bytes) ||
        !valid_extension_array_span(
            snapshot.casts, snapshot.cast_count, snapshot.cast_bytes) ||
        !valid_extension_array_span(
            snapshot.index_access_methods,
            snapshot.index_access_method_count,
            snapshot.index_access_method_bytes) ||
        !valid_extension_array_span(snapshot.optimizer_hooks,
                                    snapshot.optimizer_hook_count,
                                    snapshot.optimizer_hook_bytes) ||
        !valid_extension_array_span(
            snapshot.das_hooks, snapshot.das_hook_count,
            snapshot.das_hook_bytes) ||
        !valid_extension_array_span(snapshot.catalog_objects,
                                    snapshot.catalog_object_count,
                                    snapshot.catalog_object_bytes) ||
        (has_table_functions &&
         (!valid_extension_array_span(
              extended_snapshot.table_functions,
              extended_snapshot.table_function_count,
              extended_snapshot.table_function_bytes) ||
          !all_zero(extended_snapshot.extension_reserved,
                    sizeof(extended_snapshot.extension_reserved) /
                        sizeof(extended_snapshot.extension_reserved[0])))) ||
        !all_zero(snapshot.reserved,
                  sizeof(snapshot.reserved) / sizeof(snapshot.reserved[0]))) {
      ret = OB_INVALID_DATA;
      error = "extension snapshot count, byte span, or reserved fields are invalid";
    }
  }

  if (OB_SUCCESS == ret) {
    ret = stage_extension_array(
        snapshot.types, snapshot.type_count, snapshot.type_bytes, publication,
        requirements, error, "type",
        static_cast<int (*)(const seekdb_plugin_type_descriptor_v1_t &,
                            ObPluginExtensionSpec &, std::string &)>(
            normalize_extension));
  }
  if (OB_SUCCESS == ret) {
    ret = stage_extension_array(
        snapshot.functions, snapshot.function_count, snapshot.function_bytes,
        publication, requirements, error, "function",
        static_cast<int (*)(const seekdb_plugin_function_descriptor_v1_t &,
                            ObPluginExtensionSpec &, std::string &)>(
            normalize_extension));
  }
  if (OB_SUCCESS == ret && has_table_functions) {
    ret = stage_extension_array(
        extended_snapshot.table_functions,
        extended_snapshot.table_function_count,
        extended_snapshot.table_function_bytes, publication,
        requirements, error, "table function",
        static_cast<int (*)(
            const seekdb_plugin_table_function_descriptor_v1_t &,
            ObPluginExtensionSpec &, std::string &)>(normalize_extension));
  }
  if (OB_SUCCESS == ret) {
    ret = stage_extension_array(
        snapshot.casts, snapshot.cast_count, snapshot.cast_bytes, publication,
        requirements, error, "cast",
        static_cast<int (*)(const seekdb_plugin_cast_descriptor_v1_t &,
                            ObPluginExtensionSpec &, std::string &)>(
            normalize_extension));
  }
  if (OB_SUCCESS == ret) {
    ret = stage_extension_array(
        snapshot.index_access_methods, snapshot.index_access_method_count,
        snapshot.index_access_method_bytes, publication, requirements, error,
        "index access method",
        static_cast<int (*)(
            const seekdb_plugin_index_access_method_descriptor_v1_t &,
            ObPluginExtensionSpec &, std::string &)>(normalize_extension));
  }
  if (OB_SUCCESS == ret) {
    ret = stage_extension_array(
        snapshot.optimizer_hooks, snapshot.optimizer_hook_count,
        snapshot.optimizer_hook_bytes, publication, requirements, error,
        "optimizer hook",
        static_cast<int (*)(const seekdb_plugin_optimizer_hook_descriptor_v1_t &,
                            ObPluginExtensionSpec &, std::string &)>(
            normalize_extension));
  }
  if (OB_SUCCESS == ret) {
    ret = stage_extension_array(
        snapshot.das_hooks, snapshot.das_hook_count, snapshot.das_hook_bytes,
        publication, requirements, error, "DAS hook",
        static_cast<int (*)(const seekdb_plugin_das_hook_descriptor_v1_t &,
                            ObPluginExtensionSpec &, std::string &)>(
            normalize_extension));
  }
  if (OB_SUCCESS == ret) {
    ret = stage_extension_array(
        snapshot.catalog_objects, snapshot.catalog_object_count,
        snapshot.catalog_object_bytes, publication, requirements, error,
        "catalog object",
        static_cast<int (*)(
            const seekdb_plugin_catalog_object_descriptor_v1_t &,
            ObPluginExtensionSpec &, std::string &)>(normalize_extension));
  }
  if (OB_SUCCESS == ret) {
    requirements.requires_catalog_ = requirements.requires_catalog_ ||
        snapshot.catalog_object_count > 0;
    ret = validate_extension_requirements(requirements, manifest, error);
  }
  return ret;
}

int discover_and_stage_extensions(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_manifest_v1_t &manifest,
    const std::vector<StagedService> &manifest_services,
    const std::vector<StagedService> &dynamic_services,
    ObPluginRegistration &publication,
    std::string &error)
{
  int ret = OB_SUCCESS;
  bool found = false;
  ObPluginServiceSpec catalog;
  std::set<std::pair<std::string, uint32_t> > service_keys;
  const std::vector<StagedService> *groups[] = {
      &manifest_services, &dynamic_services};
  for (size_t group = 0; OB_SUCCESS == ret && group < 2; ++group) {
    for (size_t i = 0; OB_SUCCESS == ret && i < groups[group]->size(); ++i) {
      const ObPluginServiceSpec &candidate = (*groups[group])[i].spec_;
      const std::pair<std::string, uint32_t> key(
          candidate.name_, candidate.abi_major_);
      if (!service_keys.insert(key).second) {
        ret = OB_ENTRY_EXIST;
        error = "duplicate manifest or dynamic service";
      } else if (0 != (candidate.capabilities_ &
                       SEEKDB_PLUGIN_CAPABILITY_EXTENSION_CATALOG)) {
        if (found) {
          ret = OB_ENTRY_EXIST;
          error = "plugin exposes more than one extension catalog service";
        } else {
          catalog = candidate;
          found = true;
        }
      }
    }
  }

  seekdb_plugin_extension_catalog_service_v1_t service;
  if (OB_SUCCESS == ret && found) {
    uint32_t service_size = 0;
    std::memcpy(&service_size, catalog.service_, sizeof(service_size));
    if (catalog.abi_major_ != SEEKDB_PLUGIN_EXTENSION_SPI_MAJOR ||
        service_size < sizeof(service)) {
      ret = OB_NOT_SUPPORTED;
      error = "extension catalog service has an unsupported ABI";
    } else {
      std::memcpy(&service, catalog.service_, sizeof(service));
      if (nullptr == service.describe_extensions ||
          !all_zero(service.reserved,
                    sizeof(service.reserved) / sizeof(service.reserved[0]))) {
        ret = OB_INVALID_DATA;
        error = "extension catalog service table is invalid";
      }
    }
  }

  const seekdb_plugin_extension_snapshot_v1_t *snapshot = nullptr;
  if (OB_SUCCESS == ret && found) {
    // The loader's management reservation remains held, but the global loader
    // mutex is deliberately not held across plugin code.
    try {
      ret = from_plugin_status(
          service.describe_extensions(instance, &snapshot));
    } catch (...) {
      ret = OB_ERR_UNEXPECTED;
    }
    if (OB_SUCCESS != ret) {
      error = "extension catalog describe callback failed";
    } else {
      ret = validate_and_stage_extensions(
          snapshot, manifest, publication, error);
    }
  }
  return ret;
}

} // namespace

struct ObPluginLoader::Impl
{
  struct Module
  {
    explicit Module(PluginMemoryLimits memory_limits)
        : plugin_id_(), canonical_path_(), version_(), generation_(),
          runtime_incarnation_(), operation_id_(), handle_(INVALID_MODULE),
          manifest_(nullptr), verified_artifact_(), owner_(), host_(memory_limits), instance_(nullptr),
          dependencies_(), resolved_dependencies_(), dependency_slots_(),
          last_error_(), initialized_(false), started_(false), server_dev_admitted_(false)
    {
      std::memset(&version_, 0, sizeof(version_));
    }

    std::string plugin_id_;
    std::string canonical_path_;
    seekdb_plugin_semantic_version_t version_;
    uint64_t generation_;
    std::string runtime_incarnation_;
    std::string operation_id_;
    ModuleHandle handle_;
    const seekdb_plugin_manifest_v1_t *manifest_;
    std::unique_ptr<ObPluginVerifiedArtifact> verified_artifact_;
    std::shared_ptr<ObPluginGeneration> owner_;
    HostContext host_;
    seekdb_plugin_instance_handle_t *instance_;
    std::vector<std::unique_ptr<ObPluginLease> > dependencies_;
    std::vector<ObPluginRuntimeServiceDependency> resolved_dependencies_;
    std::vector<const void **> dependency_slots_;
    std::string last_error_;
    bool initialized_;
    bool started_;
    bool server_dev_admitted_;
  };

  Impl()
      : mutex_(), trusted_directory_(), verifier_(), activation_guard_(),
        disable_guard_(),
        registry_(), memory_limits_(), initialized_(false), shutting_down_(false), loading_(false),
        shutdown_running_(false), terminal_completed_(false),
        modules_(), active_(), disabling_(), last_error_(),
        last_failure_reason_(ObPluginLoadFailureReason::NONE)
  {}

  mutable std::mutex mutex_;
  std::string trusted_directory_;
  std::shared_ptr<const ObPluginVerifier> verifier_;
  std::shared_ptr<const ObPluginActivationGuard> activation_guard_;
  std::shared_ptr<const ObPluginDisableGuard> disable_guard_;
  std::shared_ptr<ObPluginServiceRegistry> registry_;
  PluginMemoryLimits memory_limits_;
  bool initialized_;
  bool shutting_down_;
  bool loading_;
  bool shutdown_running_;
  bool terminal_completed_;
  std::vector<std::unique_ptr<Module> > modules_;
  std::map<std::string, Module *> active_;
  std::map<std::string, uint64_t> disabling_;
  std::string last_error_;
  ObPluginLoadFailureReason last_failure_reason_;

  static void fill_status(const Module &module, ObPluginStatusSnapshot &status)
  {
    status.plugin_id_ = module.plugin_id_;
    status.canonical_path_ = module.canonical_path_;
    status.version_ = module.version_;
    status.generation_ = module.generation_;
    status.runtime_incarnation_ = module.runtime_incarnation_;
    status.operation_id_ = module.operation_id_;
    status.state_ = module.owner_ ? module.owner_->state() : ObPluginState::FAILED;
    status.lease_count_ = module.owner_ ? module.owner_->lease_count() : 0;
    seekdb_runtime_memory_usage_t memory{};
    (void)seekdb_runtime_memory_usage(module.host_.memory_.get(), &memory);
    status.host_memory_ = {memory.bytes, memory.peak_bytes, memory.allocations,
        memory.peak_allocations, memory.allocation_failures, memory.invalid_frees,
        memory.byte_limit, memory.allocation_limit};
    status.last_error_ = module.last_error_;
  }

  void set_error(const std::string &error)
  {
    try {
      if (error.empty()) {
        last_error_.clear();
      } else {
        last_error_ = error;
      }
    } catch (...) {
      last_error_.clear();
    }
  }

  seekdb_plugin_instance_handle_t *instance_for_lease(const ObPluginLease &lease, bool server_dev = false,
      std::string *runtime_incarnation = nullptr)
  {
    std::lock_guard<std::mutex> guard(mutex_);
    const char *owner = lease.owner_plugin_id();
    for (const auto &module : modules_) {
      if (owner && module->plugin_id_ == owner && module->generation_ == lease.owner_generation()) {
        if (server_dev && !module->server_dev_admitted_) return nullptr;
        if (runtime_incarnation) *runtime_incarnation = module->runtime_incarnation_;
        return module->instance_;
      }
    }
    return nullptr;
  }

  int find_bound_type(const seekdb_plugin_sql_binding_v1_t &binding,
                      ObPluginExtensionInfo &expected)
  {
    if (!registry_) return OB_NOT_INIT;
    // Validate bounded arrays before using them as C strings in registry code.
    // In particular, persisted identities with generation=0 are not executable.
    if (binding.struct_size < sizeof(binding) || binding.kind != SEEKDB_PLUGIN_EXTENSION_TYPE ||
        !valid_identifier(binding.object_id) || !valid_sql_name(binding.sql_name) ||
        !valid_identifier(binding.owner_plugin_id) || !valid_identifier(binding.physical_format_id) ||
        binding.owner_generation == 0 || binding.physical_format_version == 0 ||
        !all_zero(binding.reserved, sizeof(binding.reserved) / sizeof(binding.reserved[0]))) {
      return OB_INVALID_ARGUMENT;
    }
    std::vector<ObPluginExtensionInfo> candidates;
    uint64_t epoch = 0;
    int ret = registry_->find_extensions_by_sql_name(
        SEEKDB_PLUGIN_EXTENSION_TYPE, binding.sql_name, candidates, epoch);
    if (ret != OB_SUCCESS) return ret;
    for (const auto &candidate : candidates) {
      if (candidate.spec_.object_id_ == binding.object_id &&
          candidate.owner_plugin_id_ == binding.owner_plugin_id &&
          candidate.owner_generation_ == binding.owner_generation) {
        if (candidate.spec_.physical_format_id_ != binding.physical_format_id ||
            candidate.spec_.physical_format_version_ != binding.physical_format_version ||
            candidate.spec_.flags_ != binding.flags) return OB_STATE_NOT_MATCH;
        expected = candidate;
        return OB_SUCCESS;
      }
    }
    return OB_ENTRY_NOT_EXIST;
  }

  int find_bound_table(const seekdb_plugin_sql_binding_v1_t &binding, ObPluginExtensionInfo &expected)
  {
    if (!registry_) return OB_NOT_INIT;
    if (binding.struct_size < sizeof(binding) || binding.kind != SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION ||
        !valid_identifier(binding.object_id) || !valid_sql_name(binding.sql_name) ||
        !valid_identifier(binding.owner_plugin_id) || !binding.owner_generation || !binding.catalog_epoch ||
        binding.minimum_arity > binding.maximum_arity || binding.maximum_arity > SEEKDB_PLUGIN_MAX_ARGUMENTS ||
        !binding.column_count || !all_zero(binding.reserved, 4)) return OB_INVALID_ARGUMENT;
    std::vector<ObPluginExtensionInfo> candidates;
    uint64_t epoch = 0;
    int ret = registry_->find_extensions_by_sql_name(binding.kind, binding.sql_name, candidates, epoch);
    if (ret != OB_SUCCESS) return ret;
    if (epoch != binding.catalog_epoch) return OB_STATE_NOT_MATCH;
    for (const auto &candidate : candidates) {
      if (candidate.spec_.object_id_ == binding.object_id && candidate.owner_plugin_id_ == binding.owner_plugin_id &&
          candidate.owner_generation_ == binding.owner_generation) {
        const auto &spec = candidate.spec_;
        if (spec.minimum_arity_ != binding.minimum_arity || spec.maximum_arity_ != binding.maximum_arity ||
            spec.flags_ != binding.flags || spec.result_columns_.size() != binding.column_count) return OB_STATE_NOT_MATCH;
        expected = candidate;
        return OB_SUCCESS;
      }
    }
    return OB_ENTRY_NOT_EXIST;
  }

  int type_comparison(const seekdb_plugin_sql_binding_v1_t &binding,
      const seekdb_plugin_execution_value_v1_t *left,
      const seekdb_plugin_execution_value_v1_t *right, int32_t &ordering)
  {
    ordering = 0;
    ObPluginExtensionInfo expected;
    int ret = find_bound_type(binding, expected);
    if (ret != OB_SUCCESS) return ret;
    if (!binding.catalog_epoch || registry_->registry_epoch() != binding.catalog_epoch) return OB_STATE_NOT_MATCH;
    if ((left == nullptr) != (right == nullptr)) return OB_INVALID_ARGUMENT;
    if (left) {
      for (const auto *value : {left, right}) {
        if (value->struct_size < sizeof(*value) || value->is_null ||
            !valid_identifier(value->type_id) || expected.spec_.object_id_ != value->type_id ||
            value->data_size > UINT64_C(16777216) || (value->data_size && !value->data) ||
            !all_zero(value->reserved, 4)) return OB_INVALID_ARGUMENT;
        for (const auto byte : value->reserved_bytes) if (byte) return OB_INVALID_ARGUMENT;
      }
    }
    ObPluginExtensionLease object;
    ObPluginLease implementation;
    if ((ret = registry_->acquire_extension_with_implementation(
        expected, object, implementation, binding.catalog_epoch)) != OB_SUCCESS) return ret;
    const auto *base = static_cast<const seekdb_plugin_type_codec_service_v1_t *>(implementation.service());
    seekdb_plugin_type_compare_v1_fn compare = nullptr;
    if ((ret = validate_type_comparison_service(base, compare)) != OB_SUCCESS) return ret;
    auto *instance = instance_for_lease(implementation);
    if (!instance) return OB_ENTRY_NOT_EXIST;
    if (!left) return OB_SUCCESS; // Capability probe does not invoke native code.
    return invoke_type_comparison(compare, instance, *left, *right, ordering);
  }

  int execute_codec(const ObPluginExtensionInfo &expected,
                    const seekdb_plugin_execution_context_v1_t *context,
                    const uint8_t *encoded, uint64_t encoded_size,
                    const seekdb_plugin_execution_value_v1_t *value, bool decode)
  {
    if (!registry_) return OB_NOT_INIT;
    if (expected.spec_.kind_ != SEEKDB_PLUGIN_EXTENSION_TYPE || !context ||
        context->struct_size < sizeof(*context) || !context->emit_result ||
        (decode && (encoded_size > UINT64_C(16777216) || (encoded_size && !encoded))) ||
        (!decode && (!value || value->struct_size < sizeof(*value) ||
          (!value->is_null && (value->data_size > UINT64_C(16777216) ||
           (value->data_size && !value->data) || !valid_identifier(value->type_id)))))) {
      return OB_INVALID_ARGUMENT;
    }
    ObPluginExtensionLease object;
    ObPluginLease implementation;
    int ret = registry_->acquire_extension_with_implementation(expected, object, implementation);
    if (ret != OB_SUCCESS) return ret;
    const auto &actual = object.info()->spec_;
    if (expected.spec_.physical_format_id_ != actual.physical_format_id_ ||
        expected.spec_.physical_format_version_ != actual.physical_format_version_ ||
        (!decode && !value->is_null && actual.object_id_ != value->type_id)) {
      return OB_INVALID_ARGUMENT;
    }
    const auto *service = static_cast<const seekdb_plugin_type_codec_service_v1_t *>(implementation.service());
    if (!service || service->struct_size < sizeof(*service) ||
        service->spi_major != SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR ||
        service->reserved_word != 0 || !service->decode || !service->encode ||
        !all_zero(service->reserved, sizeof(service->reserved) / sizeof(service->reserved[0]))) {
      return OB_NOT_SUPPORTED;
    }
    auto *instance = instance_for_lease(implementation);
    if (!instance) return OB_ENTRY_NOT_EXIST;
    // Codec v1 has no SQL-context opt-in. Do not leak suffix fields to it.
    auto legacy_context = *context;
    legacy_context.struct_size = sizeof(legacy_context);
    try {
      ret = from_plugin_status(decode
          ? service->decode(instance, &legacy_context, encoded, encoded_size)
          : service->encode(instance, &legacy_context, value));
    } catch (...) { ret = OB_ERR_UNEXPECTED; }
    return ret;
  }

  int execute_lease(
      ObPluginLease &lease,
      const seekdb_plugin_execution_context_v1_t *context,
      const seekdb_plugin_execution_value_v1_t *arguments,
      const uint32_t argument_count)
  {
    if (nullptr == context || context->struct_size < sizeof(*context)) {
      return OB_INVALID_ARGUMENT;
    }
    if (!lease.is_valid() || nullptr == lease.service() ||
        lease.service_minor() < SEEKDB_PLUGIN_EXECUTION_SPI_MINOR) {
      return OB_STATE_NOT_MATCH;
    }
    const seekdb_plugin_function_service_v1_t *service =
        reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(lease.service());
    if (service->struct_size < sizeof(*service) ||
        service->spi_major != SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR ||
        service->spi_minor < SEEKDB_PLUGIN_EXECUTION_SPI_MINOR ||
        nullptr == service->execute || service->reserved_word != 0 ||
        !all_zero(service->reserved,
                  sizeof(service->reserved) / sizeof(service->reserved[0]))) {
      return OB_NOT_SUPPORTED;
    }

    auto *instance = instance_for_lease(lease);
    if (nullptr == instance) return OB_ENTRY_NOT_EXIST;

    // Do not expose appended context fields to existing binaries that require
    // exact v1 size (including GIS). SQL-aware services explicitly opt in.
    seekdb_plugin_execution_context_v1_t legacy_context;
    if (service->spi_minor < SEEKDB_PLUGIN_EXECUTION_SQL_CONTEXT_MINOR &&
        context->struct_size > sizeof(*context)) {
      legacy_context = *context;
      legacy_context.struct_size = sizeof(legacy_context);
      context = &legacy_context;
    }
    int ret = OB_SUCCESS;
    try {
      ret = from_plugin_status(service->execute(instance, context, arguments, argument_count));
    } catch (...) {
      ret = OB_ERR_UNEXPECTED;
    }
    return ret;
  }

  int resolve_result_type(const ObPluginExtensionInfo &expected, uint64_t epoch,
      const char *const *types, uint32_t count, std::string &type_id)
  {
    if (!registry_) return OB_NOT_INIT;
    if (count > SEEKDB_PLUGIN_MAX_ARGUMENTS || (count && !types)) return OB_INVALID_ARGUMENT;
    ObPluginExtensionLease object;
    ObPluginLease implementation;
    int ret = registry_->acquire_extension_with_implementation(expected, object, implementation);
    if (ret != OB_SUCCESS) return ret;
    if (registry_->registry_epoch() != epoch) return OB_STATE_NOT_MATCH;
    auto *instance = instance_for_lease(implementation);
    if (!instance) return OB_ENTRY_NOT_EXIST;
    try {
      // Execution performs the selected signature's casts before the function.
      // Resolve against those same target types, not the pre-coercion types.
      const auto &signature = object.info()->spec_.argument_type_ids_;
      std::vector<const char *> effective(count);
      for (uint32_t i = 0; i < count; ++i) {
        effective[i] = signature.empty() ? types[i]
            : signature[std::min<size_t>(i, signature.size() - 1)].c_str();
      }
      ret = resolve_function_result_type(
          static_cast<const seekdb_plugin_function_service_v1_t *>(implementation.service()),
          instance, effective.data(), count, type_id);
      if (ret == OB_SUCCESS && registry_->registry_epoch() != epoch) ret = OB_STATE_NOT_MATCH;
      if (ret != OB_SUCCESS) type_id.clear();
      return ret;
    } catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
    } catch (...) { return OB_ERR_UNEXPECTED; }
  }

  int prepare_arguments(const ObPluginExtensionSpec &function, uint64_t binding_epoch,
      const seekdb_plugin_execution_value_v1_t *arguments, uint32_t count,
      std::vector<ConvertedArgument> &converted)
  {
    if (count > SEEKDB_PLUGIN_MAX_ARGUMENTS || (count && !arguments) ||
        count < function.minimum_arity_ || count > function.maximum_arity_) return OB_INVALID_ARGUMENT;
    try {
      converted.resize(count);
      bool needs_cast = false;
      // Prepare the entire conversion set before executing any plugin code.
      for (uint32_t i = 0; i < count; ++i) {
        const auto &input = arguments[i];
        if (!valid_execution_argument(input)) return OB_INVALID_ARGUMENT;
        auto &item = converted[i];
        item.source_type_ = input.type_id ? input.type_id : "";
        item.target_type_ = function.argument_type_ids_.empty() ? item.source_type_
            : function.argument_type_ids_[std::min<size_t>(i, function.argument_type_ids_.size() - 1)];
        if (!input.type_id) continue;
        if (item.target_type_ == input.type_id) continue;
        needs_cast = true;
        ObPluginExtensionInfo selected_cast;
        uint64_t epoch = 0;
        int ret = registry_->resolve_cast(input.type_id, item.target_type_.c_str(),
            SEEKDB_PLUGIN_CAST_IMPLICIT, selected_cast, epoch);
        if (ret != OB_SUCCESS) return ret;
        if (epoch != binding_epoch) return OB_STATE_NOT_MATCH;
        ret = registry_->acquire_extension_with_implementation(selected_cast, item.object_, item.implementation_, binding_epoch);
        if (ret != OB_SUCCESS) return ret;
        if (OB_SUCCESS != (ret = validate_function_lease(item.implementation_))) return ret;
        item.instance_ = instance_for_lease(item.implementation_);
        if (!item.instance_) return OB_ENTRY_NOT_EXIST;
      }
      if (needs_cast && registry_->registry_epoch() != binding_epoch) return OB_STATE_NOT_MATCH;
      return OB_SUCCESS;
    } catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
    } catch (...) { return OB_ERR_UNEXPECTED; }
  }

  int execute_typed_lease(const ObPluginExtensionSpec &function, uint64_t binding_epoch,
      ObPluginLease &function_lease, const seekdb_plugin_execution_context_v1_t *context,
      const seekdb_plugin_execution_value_v1_t *arguments, uint32_t count)
  {
    if (!context || context->struct_size < sizeof(*context) || !context->emit_result) return OB_INVALID_ARGUMENT;
    std::vector<ConvertedArgument> prepared;
    const int ret = prepare_arguments(function, binding_epoch, arguments, count, prepared);
    if (ret != OB_SUCCESS) return ret;
    return apply_prepared_arguments(prepared, context, arguments, count,
        [&](const seekdb_plugin_execution_value_v1_t *inputs, uint32_t size) {
          return execute_lease(function_lease, context, inputs, size);
        });
  }

  int validate_manifest(const seekdb_plugin_manifest_v1_t *manifest,
                        std::vector<StagedService> &services,
                        std::string &error) const
  {
    int ret = OB_SUCCESS;
    const size_t required_size = sizeof(seekdb_plugin_manifest_v1_t);
    size_t ignored = 0;
    if (nullptr == manifest) {
      ret = OB_INVALID_DATA;
      error = "plugin entry returned a null manifest";
    } else if (manifest->struct_size < required_size ||
               manifest->abi_major != SEEKDB_PLUGIN_ABI_MAJOR ||
               manifest->abi_minor != SEEKDB_PLUGIN_ABI_MINOR) {
      ret = OB_NOT_SUPPORTED;
      error = "unsupported plugin manifest ABI; R0 requires an exact major/minor";
    } else if (!valid_identifier(manifest->plugin_id) ||
               !bounded_string(manifest->vendor, SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES,
                               ignored, true) ||
               !bounded_string(manifest->build_id, SEEKDB_PLUGIN_MAX_BUILD_ID_BYTES,
                               ignored, true)) {
      ret = OB_INVALID_DATA;
      error = "invalid plugin identity strings";
    } else if (manifest->provides_count > MAX_SERVICE_COUNT ||
               manifest->required_services_count > MAX_SERVICE_COUNT ||
               (manifest->provides_count != 0 && nullptr == manifest->provides) ||
               (manifest->required_services_count != 0 &&
                nullptr == manifest->required_services)) {
      ret = OB_SIZE_OVERFLOW;
      error = "plugin service descriptor count is invalid";
    } else if (nullptr == manifest->init || nullptr == manifest->start ||
               nullptr == manifest->stop || nullptr == manifest->deinit ||
               (manifest->capabilities & ~(KNOWN_RUNTIME_CAPABILITIES | SEEKDB_PLUGIN_CAPABILITY_SERVER_DEV)) != 0 ||
               !all_zero(manifest->reserved,
                         sizeof(manifest->reserved) / sizeof(manifest->reserved[0]))) {
      ret = OB_INVALID_DATA;
      error = "plugin lifecycle or reserved manifest fields are invalid";
    }

    if (ret == OB_SUCCESS && (manifest->capabilities & SEEKDB_PLUGIN_CAPABILITY_SERVER_DEV)) {
      if (manifest->struct_size != sizeof(seekdb_plugin_server_dev_manifest_v1_t)) {
        ret = OB_NOT_SUPPORTED;
        error = "server-dev manifest suffix size does not match the host contract";
      } else {
        const auto &contract = *reinterpret_cast<const seekdb_plugin_server_dev_manifest_v1_t *>(manifest);
        if (contract.bridge_version != SEEKDB_PLUGIN_SERVER_DEV_BRIDGE_VERSION ||
            contract.host_build_id_size == 0 || contract.host_build_id_size > sizeof(contract.host_build_id) ||
            !all_zero(contract.reserved, 4)) {
          ret = OB_NOT_SUPPORTED;
          error = "server-dev bridge version or reserved fields are invalid";
        } else {
          for (size_t i = contract.host_build_id_size; i < sizeof(contract.host_build_id); ++i) {
            if (contract.host_build_id[i] != 0) ret = OB_NOT_SUPPORTED;
          }
          if (ret != OB_SUCCESS || SEEKDB_RUNTIME_OK != seekdb_runtime_match_host_build_id(
              contract.host_build_id, contract.host_build_id_size)) {
            ret = OB_NOT_SUPPORTED;
            error = "server-dev linked host identity differs or is unavailable; rebuild against the running host";
          }
        }
      }
    }
    std::set<std::pair<std::string, uint32_t> > provide_keys;
    for (uint32_t i = 0; OB_SUCCESS == ret && i < manifest->provides_count; ++i) {
      StagedService staged;
      ret = validate_registration_service(manifest->provides[i], staged, error);
      const std::pair<std::string, uint32_t> key(staged.spec_.name_, staged.spec_.abi_major_);
      if (OB_SUCCESS == ret && !provide_keys.insert(key).second) {
        ret = OB_ENTRY_EXIST;
        error = "duplicate provided service";
      } else if (OB_SUCCESS == ret) {
        services.push_back(staged);
      }
    }

    std::set<std::pair<std::string, uint32_t> > require_keys;
    std::set<const void **> require_slots;
    for (uint32_t i = 0; OB_SUCCESS == ret && i < manifest->required_services_count; ++i) {
      const seekdb_plugin_service_require_descriptor_t &require = manifest->required_services[i];
      const size_t descriptor_size = sizeof(seekdb_plugin_service_require_descriptor_t);
      if (require.struct_size != descriptor_size || !valid_identifier(require.service_id) ||
          !valid_range(require.version_range) ||
          require.version_range.minimum_inclusive.major == 0 || require.optional > 1 ||
          (require.required_capabilities & ~KNOWN_RUNTIME_CAPABILITIES) != 0 ||
          !all_zero(require.reserved, sizeof(require.reserved) / sizeof(require.reserved[0]))) {
        ret = OB_INVALID_DATA;
        error = "invalid required service descriptor";
      } else {
        bool bytes_zero = true;
        for (size_t j = 0; bytes_zero && j < sizeof(require.reserved_bytes); ++j) {
          bytes_zero = require.reserved_bytes[j] == 0;
        }
        const std::pair<std::string, uint32_t> key(
            require.service_id, require.version_range.minimum_inclusive.major);
        if (!bytes_zero) {
          ret = OB_INVALID_DATA;
          error = "required service reserved fields are not zero";
        } else if (!require_keys.insert(key).second) {
          ret = OB_ENTRY_EXIST;
          error = "duplicate required service";
        } else if (nullptr != require.service_slot &&
                   !require_slots.insert(require.service_slot).second) {
          ret = OB_ENTRY_EXIST;
          error = "required services reuse the same service slot";
        }
      }
    }
    return ret;
  }

  int resolve_dependencies(Module &module,
                           std::string &error,
                           ObPluginLoadFailureReason &failure_reason)
  {
    int ret = OB_SUCCESS;
    try {
      module.dependencies_.reserve(module.manifest_->required_services_count);
      module.resolved_dependencies_.reserve(
          module.manifest_->required_services_count);
      module.dependency_slots_.reserve(module.manifest_->required_services_count);
    } catch (const std::bad_alloc &) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      assign_error_noexcept(error, "cannot allocate dependency tracking");
    } catch (...) {
      ret = OB_ERR_UNEXPECTED;
      assign_error_noexcept(error, "cannot prepare dependency tracking");
    }
    for (uint32_t i = 0;
         OB_SUCCESS == ret && i < module.manifest_->required_services_count;
         ++i) {
      const seekdb_plugin_service_require_descriptor_t &require =
          module.manifest_->required_services[i];
      if (nullptr != require.service_slot) *require.service_slot = nullptr;
      std::unique_ptr<ObPluginLease> lease(new (std::nothrow) ObPluginLease());
      if (!lease) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        error = "cannot allocate dependency lease";
      } else {
        int acquire_ret = registry_->acquire(
            require.service_id, require.version_range.minimum_inclusive.major,
            require.version_range.minimum_inclusive.minor,
            require.version_range.minimum_inclusive.patch,
            require.required_capabilities, *lease);
        seekdb_plugin_semantic_version_t actual = {};
        if (OB_SUCCESS == acquire_ret) {
          actual = {require.version_range.minimum_inclusive.major,
                    lease->service_minor(), lease->service_patch()};
          if (!version_in_range(actual, require.version_range)) {
            acquire_ret = OB_ENTRY_NOT_EXIST;
          }
        }
        if (OB_SUCCESS != acquire_ret) {
          lease->reset();
          if (!require.optional) {
            ret = acquire_ret;
            error = std::string("required service is unavailable: ") + require.service_id;
            failure_reason =
                ObPluginLoadFailureReason::REQUIRED_SERVICE_UNAVAILABLE;
          }
        } else {
          ObPluginRuntimeServiceDependency dependency;
          dependency.service_id_ = require.service_id;
          dependency.requested_version_ = require.version_range;
          dependency.required_capabilities_ = require.required_capabilities;
          dependency.optional_ = 0 != require.optional;
          dependency.provider_plugin_id_ = lease->owner_plugin_id();
          dependency.provider_generation_ = lease->owner_generation();
          dependency.provider_version_ = actual;
          module.resolved_dependencies_.push_back(std::move(dependency));
          module.dependencies_.push_back(std::move(lease));
          if (nullptr != require.service_slot) {
            module.dependency_slots_.push_back(require.service_slot);
            *require.service_slot = module.dependencies_.back()->service();
          }
        }
      }
    }
    return ret;
  }

  void release_dependencies(Module &module)
  {
    for (auto it = module.dependency_slots_.rbegin(); it != module.dependency_slots_.rend(); ++it) {
      if (nullptr != *it) **it = nullptr;
    }
    module.dependency_slots_.clear();
    for (auto it = module.dependencies_.rbegin(); it != module.dependencies_.rend(); ++it) {
      (*it)->reset();
    }
    module.dependencies_.clear();
    module.resolved_dependencies_.clear();
  }

  void fail_generation(Module &module, const std::string &error)
  {
    try {
      module.last_error_ = error;
    } catch (...) {
      module.last_error_.clear();
    }
    if (module.owner_ && module.owner_->state() != ObPluginState::FAILED &&
        module.owner_->state() != ObPluginState::STOPPED) {
      (void)module.owner_->transition_to(ObPluginState::FAILED);
    }
  }

  void block_generation(Module &module, const std::string &error)
  {
    try {
      module.last_error_ = error;
    } catch (...) {
      module.last_error_.clear();
    }
    if (module.owner_ && module.owner_->state() != ObPluginState::BLOCKED &&
        module.owner_->state() != ObPluginState::STOPPED) {
      (void)module.owner_->transition_to(ObPluginState::BLOCKED);
    }
  }

  // No loader mutex is held while this routine waits or calls plugin code.
  // The caller owns a disable or terminal-shutdown lifecycle reservation,
  // which pins the heap Module and rejects competing management operations.
  int disable_runtime(Module &module,
                      const int64_t timeout_us,
                      const bool allow_blocked_retry,
                      ObPluginDisablePermit *disable_permit,
                      bool &stop_checkpoint_failed,
                      const ObPluginTerminalStopAuthority *terminal_authority,
                      ObPluginRuntimeDisableResult &result,
                      std::string &error)
  {
    int ret = OB_SUCCESS;
    stop_checkpoint_failed = false;
    result = ObPluginRuntimeDisableResult();
    result.generation_ = module.generation_;
    ObPluginState state = module.owner_->state();
    result.actual_state_ = state;
    const bool retry_blocked = allow_blocked_retry &&
                               ObPluginState::BLOCKED == state;

    if (ObPluginState::ACTIVE == state) {
      result.phase_ = ObPluginDisablePhase::QUIESCE;
      ret = registry_->quiesce(module.owner_);
    } else if (ObPluginState::QUIESCING != state && !retry_blocked) {
      ret = OB_STATE_NOT_MATCH;
      assign_error_noexcept(error, "plugin is not active");
    }

    if (OB_SUCCESS == ret && !retry_blocked) {
      result.phase_ = ObPluginDisablePhase::DRAIN;
      ret = module.owner_->wait_for_drain(timeout_us);
    }
    if (OB_SUCCESS == ret && module.started_) {
      if (nullptr != disable_permit) {
        std::string checkpoint_error;
        const int checkpoint_ret =
            disable_permit->record_stop_entered(checkpoint_error);
        if (OB_SUCCESS != checkpoint_ret) {
          ret = checkpoint_ret;
          stop_checkpoint_failed = true;
          assign_error_noexcept(
              error,
              checkpoint_error.empty()
                  ? "plugin stop checkpoint failed; stop callback was not entered"
                  : checkpoint_error.c_str());
        }
      }
      if (OB_SUCCESS == ret) {
        result.phase_ = ObPluginDisablePhase::STOP;
        result.stop_entered_ = true;
        const int stop_ret =
            call_lifecycle(module.manifest_->stop, module.instance_);
        if (OB_SUCCESS == stop_ret) {
          module.started_ = false;
        } else {
          ret = stop_ret;
          assign_error_noexcept(
              error, "plugin stop callback failed; module is blocked until process-exit retry");
          if (module.owner_->state() != ObPluginState::BLOCKED &&
              OB_SUCCESS !=
                  module.owner_->transition_to(ObPluginState::BLOCKED)) {
            append_error_noexcept(
                error, "; failed to record blocked runtime state");
          }
        }
      }
    }
    if (OB_SUCCESS == ret) {
      if (module.initialized_) {
        result.phase_ = ObPluginDisablePhase::DEINIT;
        call_deinit(module.manifest_->deinit, module.instance_);
        module.initialized_ = false;
        module.instance_ = nullptr;
      }
      cleanup_host_resources(module.host_);
      release_dependencies(module);
      result.phase_ = ObPluginDisablePhase::MARK_STOPPED;
      ret = retry_blocked && nullptr != terminal_authority
                ? registry_->mark_stopped(module.owner_, *terminal_authority)
                : registry_->mark_stopped(module.owner_);
      if (OB_SUCCESS != ret) {
        assign_error_noexcept(error, "failed to mark plugin stopped");
      }
    }
    if (OB_SUCCESS != ret) {
      if (error.empty()) {
        assign_error_noexcept(
            error, ret == OB_TIMEOUT ? "timed out draining plugin leases" :
                                      "plugin disable failed");
      }
    } else {
      result.phase_ = ObPluginDisablePhase::COMPLETE;
    }
    result.status_ = ret;
    result.actual_state_ = module.owner_->state();
    assign_error_noexcept(result.error_, error.c_str());
    return ret;
  }
};

namespace
{

bool valid_custom_service(const seekdb_plugin_custom_executor_v1_t *service)
{
  return service && service->struct_size == sizeof(*service) && service->spi_major == 1 &&
      service->spi_minor <= 2 && !service->reserved_word && service->open && service->next &&
      service->rescan && service->close && all_zero(service->reserved, 4);
}

bool valid_custom_values(const seekdb_plugin_execution_value_v1_t *values, uint32_t count)
{
  if (count > SEEKDB_PLUGIN_CUSTOM_MAX_COLUMNS || (count && !values)) return false;
  uint64_t bytes = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const auto &value = values[i];
    if (!valid_execution_argument(value) || !valid_identifier(value.type_id) ||
        (value.is_null && (value.data_size || value.data))) return false;
    if (value.data_size > SEEKDB_PLUGIN_CUSTOM_MAX_ROW_BYTES - bytes) return false;
    bytes += value.data_size;
  }
  return true;
}

uint32_t required_custom_encoding(const char *id)
{
  if (!std::strcmp(id, "core.type.bytes")) return SEEKDB_PLUGIN_CUSTOM_ENCODING_BYTES;
  if (!std::strcmp(id, "core.type.null")) return SEEKDB_PLUGIN_CUSTOM_ENCODING_NULL;
  constexpr char core[] = "core.type.", gis[] = "org.seekdb.gis.scalar.";
  const char *name = !std::strncmp(id, core, sizeof(core) - 1) ? id + sizeof(core) - 1 :
      !std::strncmp(id, gis, sizeof(gis) - 1) ? id + sizeof(gis) - 1 : nullptr;
  if (name) {
    const char *names[] = {"bool", "int32", "uint32", "int64", "uint64", "float64"};
    const uint32_t encodings[] = {SEEKDB_PLUGIN_CUSTOM_ENCODING_BOOL, SEEKDB_PLUGIN_CUSTOM_ENCODING_INT32,
        SEEKDB_PLUGIN_CUSTOM_ENCODING_UINT32, SEEKDB_PLUGIN_CUSTOM_ENCODING_INT64,
        SEEKDB_PLUGIN_CUSTOM_ENCODING_UINT64, SEEKDB_PLUGIN_CUSTOM_ENCODING_FLOAT64};
    for (uint32_t i = 0; i < 6; ++i) if (!std::strcmp(name, names[i])) return encodings[i];
  }
  return UINT32_MAX; // Custom logical IDs declare their own representation.
}
bool valid_custom_schema(const seekdb_plugin_custom_schema_v1_t &schema)
{
  if (schema.struct_size != sizeof(schema) || schema.column_count > SEEKDB_PLUGIN_CUSTOM_MAX_COLUMNS ||
      (schema.column_count && !schema.columns) || !all_zero(schema.reserved, 4)) return false;
  for (uint32_t i = 0; i < schema.column_count; ++i) {
    const auto &column = schema.columns[i];
    if (column.struct_size != sizeof(column) || column.reserved_word ||
        (column.flags & ~(SEEKDB_PLUGIN_CUSTOM_COLUMN_NULLABLE | SEEKDB_PLUGIN_CUSTOM_COLUMN_STORED)) ||
        column.encoding > SEEKDB_PLUGIN_CUSTOM_ENCODING_FLOAT64 || !all_zero(column.reserved, 4) ||
        !std::memchr(column.type_id, 0, sizeof(column.type_id)) || !valid_identifier(column.type_id)) return false;
    const uint32_t required = required_custom_encoding(column.type_id);
    if (required != UINT32_MAX && required != column.encoding) return false;
    if (column.encoding == SEEKDB_PLUGIN_CUSTOM_ENCODING_NULL && !(column.flags & SEEKDB_PLUGIN_CUSTOM_COLUMN_NULLABLE))
      return false;
  }
  return true;
}
bool custom_values_match_schema(const seekdb_plugin_execution_value_v1_t *values, uint32_t count,
                               const seekdb_plugin_custom_schema_v1_t *schema)
{
  if (!schema) return true; // Explicit v1 context, not a zero-column schema.
  if (count != schema->column_count) return false;
  for (uint32_t i = 0; i < count; ++i) {
    const auto &value = values[i]; const auto &column = schema->columns[i];
    if (std::strcmp(value.type_id, column.type_id)) return false;
    if (value.is_null) {
      if (!(column.flags & SEEKDB_PLUGIN_CUSTOM_COLUMN_NULLABLE)) return false;
    } else {
      const uint32_t encoding = column.encoding;
      if (encoding == SEEKDB_PLUGIN_CUSTOM_ENCODING_NULL) return false;
      if (encoding != SEEKDB_PLUGIN_CUSTOM_ENCODING_BYTES) {
        const uint64_t width = encoding == SEEKDB_PLUGIN_CUSTOM_ENCODING_BOOL ? 1 :
            encoding == SEEKDB_PLUGIN_CUSTOM_ENCODING_INT32 || encoding == SEEKDB_PLUGIN_CUSTOM_ENCODING_UINT32 ? 4 : 8;
        if (value.data_size != width || (encoding == SEEKDB_PLUGIN_CUSTOM_ENCODING_BOOL && value.data[0] > 1)) return false;
      }
    }
  }
  return true;
}

class CustomExecutorCursor final : public ICustomExecutor
{
public:
  CustomExecutorCursor(ObPluginLease &&lease, seekdb_plugin_instance_handle_t *instance,
      const seekdb_plugin_custom_executor_v1_t &service)
      : lease_(std::move(lease)), instance_(instance), service_(service) {}
  ~CustomExecutorCursor() override { static_cast<void>(close()); }
  int open(const uint8_t *plan, uint32_t size) {
    int ret = OB_ERR_UNEXPECTED;
    try { ret = from_plugin_status(service_.open(instance_, plan, size, &cursor_)); }
    catch (const std::bad_alloc &) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    catch (...) { ret = OB_ERR_UNEXPECTED; }
    if (ret == OB_ITER_END || (ret == OB_SUCCESS && !cursor_)) ret = OB_INVALID_DATA;
    return ret;
  }
  int next(const seekdb_plugin_custom_context_v1_t &context) override {
    if (!cursor_) return OB_NOT_INIT;
    if (failed_) return OB_STATE_NOT_MATCH;
    const bool bound = context.struct_size == sizeof(seekdb_plugin_custom_context_v4_t);
    const bool controlled = bound || context.struct_size == sizeof(seekdb_plugin_custom_context_v3_t);
    const bool described = controlled || context.struct_size == sizeof(seekdb_plugin_custom_context_v2_t);
    if ((!described && context.struct_size != sizeof(context)) || !context.host_context || !context.next_input ||
        !context.emit || !context.check_interrupt || context.reserved_word ||
        context.input_count > SEEKDB_PLUGIN_CUSTOM_MAX_INPUTS ||
        context.output_column_count > SEEKDB_PLUGIN_CUSTOM_MAX_COLUMNS || !all_zero(context.reserved, 4)) {
      failed_ = true;
      return OB_INVALID_ARGUMENT;
    }
    const auto *schemas = described ? reinterpret_cast<const seekdb_plugin_custom_context_v2_t *>(&context) : nullptr;
    const auto *control = controlled ? reinterpret_cast<const seekdb_plugin_custom_context_v3_t *>(&context) : nullptr;
    const auto *bindings = bound ? reinterpret_cast<const seekdb_plugin_custom_context_v4_t *>(&context) : nullptr;
    if (bindings && (!bindings->bind_rescan_input || !all_zero(bindings->reserved, 4))) {
      failed_ = true; return OB_INVALID_ARGUMENT;
    }
    if (control && (!control->rescan_input || !all_zero(control->reserved, 4))) {
      failed_ = true; return OB_INVALID_ARGUMENT;
    }
    if (schemas) {
      bool valid = (!context.input_count || schemas->inputs) && schemas->output && all_zero(schemas->reserved, 4);
      for (uint32_t i = 0; valid && i < context.input_count; ++i) valid = valid_custom_schema(schemas->inputs[i]);
      if (!valid || !valid_custom_schema(*schemas->output) || schemas->output->column_count != context.output_column_count) {
        failed_ = true; return OB_INVALID_ARGUMENT;
      }
    }
    struct Call {
      const seekdb_plugin_custom_context_v1_t &host;
      const seekdb_plugin_custom_context_v2_t *schemas;
      const seekdb_plugin_custom_context_v3_t *control;
      const seekdb_plugin_custom_context_v4_t *bindings;
      int error = OB_SUCCESS;
      uint32_t emitted = 0;
      seekdb_plugin_status_t save(seekdb_plugin_status_t status, int database_error, bool allow_end = false) {
        if (!error) {
          if (database_error) error = database_error == OB_ITER_END ? OB_INVALID_DATA : database_error;
          else if (status == SEEKDB_PLUGIN_STATUS_END_OF_STREAM && !allow_end) error = OB_INVALID_DATA;
          else if (status != SEEKDB_PLUGIN_STATUS_OK && status != SEEKDB_PLUGIN_STATUS_END_OF_STREAM)
            error = from_plugin_status(status);
        }
        return error ? to_plugin_status(error) : status;
      }
      static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL input(void *opaque, uint32_t index,
          seekdb_plugin_custom_row_v1_t *row, int32_t *error) noexcept {
        auto &self = *static_cast<Call *>(opaque);
        int db = 0;
        seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK;
        try {
          if (!row || !error || index >= self.host.input_count || row->struct_size != sizeof(*row))
            db = OB_INVALID_ARGUMENT;
          else if (!self.error) {
            *row = {sizeof(*row), 0, nullptr, {0}};
            status = self.host.next_input(self.host.host_context, index, row, &db);
            if (!db && status == SEEKDB_PLUGIN_STATUS_OK && (row->struct_size != sizeof(*row) ||
                !all_zero(row->reserved, 4) || !valid_custom_values(row->values, row->column_count) ||
                !custom_values_match_schema(row->values, row->column_count, self.schemas ? &self.schemas->inputs[index] : nullptr)))
              db = OB_INVALID_DATA;
          }
        } catch (const std::bad_alloc &) { db = OB_ALLOCATE_MEMORY_FAILED; }
        catch (...) { db = OB_ERR_UNEXPECTED; }
        status = self.save(status, db, true);
        if (status != SEEKDB_PLUGIN_STATUS_OK && row) *row = {sizeof(*row), 0, nullptr, {0}};
        if (error) *error = self.error;
        return status;
      }
      static seekdb_plugin_status_t reset_input(void *opaque, uint32_t index, int32_t *error, bool bind) noexcept {
        auto &self = *static_cast<Call *>(opaque);
        int db = 0;
        seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK;
        try {
          if (!error || !self.control || (bind && !self.bindings) || index >= self.host.input_count || self.emitted)
            db = OB_INVALID_ARGUMENT;
          else if (!self.error) {
            poll(opaque, &db);
            if (!db) status = bind ? self.bindings->bind_rescan_input(self.host.host_context, index, &db) :
                self.control->rescan_input(self.host.host_context, index, &db);
            self.save(status, db);
            if (!self.error) poll(opaque, &db);
          }
        } catch (const std::bad_alloc &) { db = OB_ALLOCATE_MEMORY_FAILED; }
        catch (...) { db = OB_ERR_UNEXPECTED; }
        status = self.save(status, db);
        if (error) *error = self.error;
        return status;
      }
      static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL rewind(void *opaque, uint32_t index, int32_t *error) noexcept {
        return reset_input(opaque, index, error, false);
      }
      static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL bind_rewind(void *opaque, uint32_t index, int32_t *error) noexcept {
        return reset_input(opaque, index, error, true);
      }
      static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit(void *opaque,
          const seekdb_plugin_execution_value_v1_t *values, uint32_t count, int32_t *error) noexcept {
        auto &self = *static_cast<Call *>(opaque);
        int db = 0;
        seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK;
        try {
          if (!error || self.emitted || count != self.host.output_column_count || !valid_custom_values(values, count) ||
              !custom_values_match_schema(values, count, self.schemas ? self.schemas->output : nullptr))
            db = OB_INVALID_ARGUMENT;
          else if (!self.error) {
            ++self.emitted;
            status = self.host.emit(self.host.host_context, values, count, &db);
          }
        } catch (const std::bad_alloc &) { db = OB_ALLOCATE_MEMORY_FAILED; }
        catch (...) { db = OB_ERR_UNEXPECTED; }
        status = self.save(status, db);
        if (error) *error = self.error;
        return status;
      }
      static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL poll(void *opaque, int32_t *error) noexcept {
        auto &self = *static_cast<Call *>(opaque);
        int db = 0;
        seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK;
        try {
          if (!error) db = OB_INVALID_ARGUMENT;
          else if (!self.error) status = self.host.check_interrupt(self.host.host_context, &db);
        } catch (...) { db = OB_ERR_UNEXPECTED; }
        status = self.save(status, db);
        if (error) *error = self.error;
        return status;
      }
    } call{context, schemas, control, bindings};
    seekdb_plugin_custom_context_v4_t bound_view = {};
    auto &controlled_view = bound_view.v3;
    auto &extended = controlled_view.v2;
    if (schemas) extended = *schemas;
    auto &view = extended.v1;
    view = context;
    if (controlled && service_.spi_minor == 0) view.struct_size = sizeof(extended);
    else if (bound && service_.spi_minor == 1) view.struct_size = sizeof(controlled_view);
    controlled_view.rescan_input = Call::rewind;
    bound_view.bind_rescan_input = Call::bind_rewind;
    view.host_context = &call; view.next_input = Call::input; view.emit = Call::emit; view.check_interrupt = Call::poll;
    int32_t error = 0;
    Call::poll(&call, &error);
    int ret = error;
    if (ret == OB_SUCCESS && ended_) return OB_ITER_END;
    if (ret == OB_SUCCESS) {
      try { ret = from_plugin_status(service_.next(instance_, cursor_, &view)); }
      catch (const std::bad_alloc &) { ret = OB_ALLOCATE_MEMORY_FAILED; }
      catch (...) { ret = OB_ERR_UNEXPECTED; }
      if (!call.error && ret != OB_SUCCESS && ret != OB_ITER_END) call.error = ret;
      Call::poll(&call, &error);
      if (error) ret = error;
      else if ((ret == OB_SUCCESS && call.emitted != 1) || (ret == OB_ITER_END && call.emitted)) ret = OB_INVALID_DATA;
    }
    failed_ = ret != OB_SUCCESS && ret != OB_ITER_END;
    ended_ = ret == OB_ITER_END;
    return ret;
  }
  int rescan() override {
    if (!cursor_) return OB_NOT_INIT;
    int ret = OB_ERR_UNEXPECTED;
    try { ret = from_plugin_status(service_.rescan(instance_, cursor_)); }
    catch (...) { ret = OB_ERR_UNEXPECTED; }
    if (ret == OB_ITER_END) ret = OB_INVALID_DATA;
    failed_ = ret != OB_SUCCESS; ended_ = false;
    return ret;
  }
  int close() override {
    int ret = OB_SUCCESS;
    if (cursor_) {
      auto *cursor = cursor_; cursor_ = nullptr;
      try { ret = from_plugin_status(service_.close(instance_, cursor)); }
      catch (...) { ret = OB_ERR_UNEXPECTED; }
      if (ret == OB_ITER_END) ret = OB_INVALID_DATA;
    }
    lease_.reset();
    return ret;
  }
private:
  ObPluginLease lease_;
  seekdb_plugin_instance_handle_t *instance_;
  const seekdb_plugin_custom_executor_v1_t service_;
  void *cursor_ = nullptr;
  bool failed_ = false, ended_ = false;
};

class PluginTableCursor final : public IPluginTableCursor
{
public:
  PluginTableCursor(ObPluginExtensionLease &&extension_lease,
                    ObPluginLease &&implementation_lease,
                    seekdb_plugin_instance_handle_t *instance,
                    const seekdb_plugin_table_function_service_v1_t *service,
                    seekdb_plugin_table_cursor_handle_t *cursor,
                    std::vector<ConvertedArgument> &&arguments)
      : extension_lease_(std::move(extension_lease)),
        implementation_lease_(std::move(implementation_lease)),
        instance_(instance), service_(service), cursor_(cursor), arguments_(std::move(arguments)),
        failed_(false), strict_empty_(false)
  {}

  ~PluginTableCursor() override { static_cast<void>(close()); }

  int next(const seekdb_plugin_table_execution_context_v1_t *context,
           const uint32_t maximum_rows,
           uint32_t *emitted_rows) override
  {
    if (nullptr == cursor_ || nullptr == context ||
        context->struct_size < sizeof(*context) ||
        nullptr == context->emit_row || nullptr == emitted_rows ||
        maximum_rows == 0) {
      return OB_INVALID_ARGUMENT;
    }
    *emitted_rows = 0;
    if (failed_) return OB_STATE_NOT_MATCH;
    if (strict_empty_) return OB_ITER_END;
    auto legacy = *context;
    seekdb_plugin_table_execution_context_v2_t control{};
    seekdb_plugin_table_execution_context_v3_t sql{};
    if (service_->spi_minor < SEEKDB_PLUGIN_TABLE_QUERY_CONTROL_MINOR && context->struct_size > sizeof(*context)) {
      legacy.struct_size = sizeof(legacy); context = &legacy;
    } else if (service_->spi_minor == SEEKDB_PLUGIN_TABLE_QUERY_CONTROL_MINOR && context->struct_size > sizeof(control)) {
      control = *reinterpret_cast<const seekdb_plugin_table_execution_context_v2_t *>(context);
      control.v1.struct_size = sizeof(control); context = &control.v1;
    } else if (service_->spi_minor == SEEKDB_PLUGIN_TABLE_SQL_CONTEXT_MINOR && context->struct_size > sizeof(sql)) {
      sql = *reinterpret_cast<const seekdb_plugin_table_execution_context_v3_t *>(context);
      sql.v2.v1.struct_size = sizeof(sql); context = &sql.v2.v1;
    }
    try {
      const int ret = from_plugin_status(service_->next(
          instance_, cursor_, context, maximum_rows, emitted_rows));
      if (*emitted_rows > maximum_rows ||
          (ret == OB_ITER_END && *emitted_rows != 0)) {
        failed_ = true;
        return OB_INVALID_DATA;
      }
      if (ret != OB_SUCCESS && ret != OB_ITER_END) failed_ = true;
      return ret;
    } catch (...) {
      failed_ = true;
      return OB_ERR_UNEXPECTED;
    }
  }

  int rescan(const seekdb_plugin_execution_value_v1_t *arguments,
             const uint32_t argument_count) override
  {
    if (nullptr == cursor_ ||
        (argument_count != 0 && nullptr == arguments)) {
      return OB_INVALID_ARGUMENT;
    }
    try {
      seekdb_plugin_execution_context_v1_t context = {}; context.struct_size = sizeof(context);
      failed_ = true;
      const int ret = apply_prepared_arguments(arguments_, &context, arguments, argument_count,
          [&](const seekdb_plugin_execution_value_v1_t *inputs, uint32_t count) {
            strict_empty_ = null_propagating_table_input(extension_lease_.info()->spec_.flags_, inputs, count);
            // Keep the existing cursor owned but dormant. A later non-NULL
            // rescan can reset it without retaining an old execution context.
            if (strict_empty_) return OB_SUCCESS;
            return from_plugin_status(service_->rescan(instance_, cursor_, inputs, count));
          });
      failed_ = ret != OB_SUCCESS;
      return ret;
    } catch (...) {
      return OB_ERR_UNEXPECTED;
    }
  }

  int close() override
  {
    if (nullptr == cursor_) return OB_SUCCESS;
    int ret = OB_SUCCESS;
    try {
      ret = from_plugin_status(service_->close(instance_, cursor_));
    } catch (...) {
      ret = OB_ERR_UNEXPECTED;
    }
    cursor_ = nullptr;
    arguments_.clear();
    implementation_lease_.reset();
    extension_lease_.reset();
    return ret;
  }

private:
  ObPluginExtensionLease extension_lease_;
  ObPluginLease implementation_lease_;
  seekdb_plugin_instance_handle_t *instance_;
  const seekdb_plugin_table_function_service_v1_t *service_;
  seekdb_plugin_table_cursor_handle_t *cursor_;
  std::vector<ConvertedArgument> arguments_;
  bool failed_;
  bool strict_empty_;
};

} // namespace

ObPluginArtifactMetadata::ObPluginArtifactMetadata()
    : plugin_id_(), build_id_(), package_digest_(), package_version_(),
      catalog_version_(0), data_format_version_(0)
{
  std::memset(&package_version_, 0, sizeof(package_version_));
}

ObPluginStatusSnapshot::ObPluginStatusSnapshot()
    : plugin_id_(), canonical_path_(), version_(), generation_(0),
      runtime_incarnation_(), operation_id_(),
      state_(ObPluginState::DISCOVERED), lease_count_(0), host_memory_(), last_error_()
{
  std::memset(&version_, 0, sizeof(version_));
}

ObPluginActivationRequest::ObPluginActivationRequest()
    : mode_(ObPluginActivationMode::ACTIVATE), relative_path_(), plugin_id_(),
      build_id_(), package_digest_(), package_version_(), catalog_version_(0),
      data_format_version_(0), expected_generation_(0),
      expected_runtime_incarnation_(), expected_operation_id_()
{
  std::memset(&package_version_, 0, sizeof(package_version_));
}

ObPluginRuntimeServiceDependency::ObPluginRuntimeServiceDependency()
    : service_id_(), requested_version_(), required_capabilities_(0),
      optional_(false), provider_plugin_id_(), provider_generation_(0),
      provider_version_()
{
  std::memset(&requested_version_, 0, sizeof(requested_version_));
  std::memset(&provider_version_, 0, sizeof(provider_version_));
}

ObPluginRuntimeActivationResult::ObPluginRuntimeActivationResult()
    : status_(OB_STATE_NOT_MATCH), generation_(0), runtime_incarnation_(),
      operation_id_(), actual_state_(ObPluginState::DISCOVERED),
      phase_(ObPluginActivationPhase::NONE), start_entered_(false),
      candidate_prepared_(false), candidate_base_epoch_(0), services_(),
      extensions_(), dependencies_(), error_()
{
}

ObPluginRecoveryActivation::ObPluginRecoveryActivation()
    : relative_path_(), plugin_id_(), package_digest_(), generation_(0),
      runtime_incarnation_(), operation_id_()
{
}

ObPluginRuntimeDisableResult::ObPluginRuntimeDisableResult()
    : status_(OB_STATE_NOT_MATCH), generation_(0),
      actual_state_(ObPluginState::DISCOVERED),
      phase_(ObPluginDisablePhase::NONE), stop_entered_(false), error_()
{
}

ObPluginLoader::ObPluginLoader() : impl_(new (std::nothrow) Impl())
{
}

ObPluginLoader::~ObPluginLoader()
{
  if (impl_) {
    // Destruction is not proof that the process has entered terminal shutdown.
    // Never dlclose here.  If the caller omitted shutdown_for_process_exit(),
    // retain the whole ownership domain (including registry and policy objects)
    // so callbacks cannot observe freed host state.
    bool retain_until_process_exit = false;
    {
      std::lock_guard<std::mutex> guard(impl_->mutex_);
      for (std::unique_ptr<Impl::Module> &module : impl_->modules_) {
        if (module->handle_ != INVALID_MODULE) {
          retain_until_process_exit = true;
          break;
        }
      }
    }
    if (retain_until_process_exit) {
      std::fprintf(stderr,
                   "seekdb plugin loader destroyed before terminal shutdown; "
                   "retaining runtime domain until process exit\n");
      (void)impl_.release();
    }
  }
}

int ObPluginLoader::init(const std::string &trusted_directory,
                         const std::shared_ptr<const ObPluginVerifier> &verifier,
                         const std::shared_ptr<const ObPluginActivationGuard> &activation_guard,
                         const std::shared_ptr<const ObPluginDisableGuard> &disable_guard,
                         const std::shared_ptr<ObPluginServiceRegistry> &registry,
                         PluginMemoryLimits memory_limits)
{
  if (!impl_) return OB_ALLOCATE_MEMORY_FAILED;
  std::lock_guard<std::mutex> guard(impl_->mutex_);
  int ret = OB_SUCCESS;
  std::string canonical;
  std::string error;
  try {
    if (impl_->terminal_completed_) {
      ret = OB_STATE_NOT_MATCH;
      error = "plugin loader already completed terminal process shutdown";
    } else if (impl_->initialized_) {
      ret = OB_INIT_TWICE;
      error = "plugin loader is already initialized";
    } else if (!verifier || !activation_guard || !disable_guard || !registry ||
               trusted_directory.empty()) {
      ret = OB_INVALID_ARGUMENT;
      error = "trusted directory, verifier, activation/disable guards and registry are mandatory";
    } else if (OB_SUCCESS !=
               (ret = canonical_existing(trusted_directory, true, canonical, error))) {
    } else {
      impl_->trusted_directory_ = canonical;
      impl_->verifier_ = verifier;
      impl_->activation_guard_ = activation_guard;
      impl_->disable_guard_ = disable_guard;
      impl_->registry_ = registry;
      impl_->memory_limits_ = memory_limits;
      impl_->shutting_down_ = false;
      impl_->loading_ = false;
      impl_->shutdown_running_ = false;
      impl_->initialized_ = true;
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    assign_error_noexcept(error, "plugin loader initialization allocation failed");
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
    assign_error_noexcept(error, "unexpected plugin loader initialization failure");
  }
  if (OB_SUCCESS != ret) impl_->set_error(error);
  return ret;
}

bool ObPluginLoader::is_initialized() const
{
  if (!impl_) return false;
  std::lock_guard<std::mutex> guard(impl_->mutex_);
  return impl_->initialized_;
}

int ObPluginLoader::load(const std::string &relative_path,
                         uint64_t *loaded_generation)
{
  return activate_internal(relative_path, nullptr, loaded_generation);
}

int ObPluginLoader::recover_startup_activation(
    const ObPluginRecoveryActivation &recovery,
    uint64_t *loaded_generation)
{
  return activate_internal(recovery.relative_path_, &recovery,
                           loaded_generation);
}

int ObPluginLoader::activate_internal(
    const std::string &relative_path,
    const ObPluginRecoveryActivation *recovery,
    uint64_t *loaded_generation)
{
  if (!impl_)
    return OB_ALLOCATE_MEMORY_FAILED;
  int ret = OB_SUCCESS;
  std::string error;
  std::string catalog_error;
  std::string canonical;
  std::string trusted_directory;
  std::shared_ptr<const ObPluginVerifier> verifier;
  std::shared_ptr<const ObPluginActivationGuard> activation_guard;
  std::unique_ptr<Impl::Module> module;
  std::vector<StagedService> manifest_services;
  ObPluginRegistration publication;
  ObPluginActivationCandidate candidate;
  std::unique_ptr<ObPluginActivationPermit> activation_permit;
  std::unique_ptr<ObPluginActivationCommit> activation_commit;
  ObPluginActivationRequest activation_request;
  ObPluginRuntimeActivationResult activation_result;
  ObPluginActivationDecision activation_decision =
      OB_PLUGIN_ACTIVATION_UNKNOWN;
  ObPluginLoadFailureReason failure_reason =
      ObPluginLoadFailureReason::NONE;
  seekdb_plugin_entry_v1_fn entry = nullptr;
  bool publication_open = false;
  bool active_placeholder = false;
  bool load_reserved = false;
  bool permit_issued = false;
  bool commit_attempted = false;
  bool catalog_committed = false;
  bool promoted = false;
  bool identity_must_remain = false;
  Impl::Module *promoted_module = nullptr;

  try {
    if (nullptr != loaded_generation)
      *loaded_generation = 0;
    {
      std::lock_guard<std::mutex> guard(impl_->mutex_);
      impl_->last_failure_reason_ = ObPluginLoadFailureReason::NONE;
      if (!impl_->initialized_) {
        ret = OB_NOT_INIT;
        error = "plugin loader is not initialized";
      } else if (impl_->shutting_down_) {
        ret = OB_STATE_NOT_MATCH;
        error = "plugin loader is shutting down";
      } else if (impl_->loading_ || !impl_->disabling_.empty()) {
        ret = OB_EAGAIN;
        error = "another plugin management operation is in progress";
      } else if (!safe_relative_path(relative_path) ||
                 (nullptr != recovery &&
                  (!valid_identifier(recovery->plugin_id_) ||
                   !valid_digest(recovery->package_digest_) ||
                   0 == recovery->generation_ ||
                   !valid_identifier(recovery->runtime_incarnation_) ||
                   !valid_identifier(recovery->operation_id_)))) {
        ret = OB_INVALID_ARGUMENT;
        error = "plugin path or startup recovery identity is invalid";
      } else {
        trusted_directory = impl_->trusted_directory_;
        verifier = impl_->verifier_;
        activation_guard = impl_->activation_guard_;
        impl_->loading_ = true;
        load_reserved = true;
      }
    }
    if (OB_SUCCESS == ret) {
      module.reset(new Impl::Module(impl_->memory_limits_));
      const std::string candidate = trusted_directory +
#if defined(_WIN32)
                                    "\\" + relative_path;
#else
                                    "/" + relative_path;
#endif
      ret = canonical_existing(candidate, false, canonical, error);
      if (OB_SUCCESS == ret && !contains_path(trusted_directory, canonical)) {
        ret = OB_INVALID_ARGUMENT;
        error = "plugin resolves outside the trusted directory";
      }
    }

    if (OB_SUCCESS == ret) {
      try {
        ret = verifier->verify_and_pin(canonical, module->verified_artifact_, error);
      } catch (const std::bad_alloc &) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        error = "verifier allocation failed";
      } catch (...) {
        ret = OB_ERR_UNEXPECTED;
        error = "verifier threw an exception";
      }
      if (OB_SUCCESS == ret && !module->verified_artifact_) {
        ret = OB_INVALID_DATA;
        error = "verifier returned no immutable artifact lease";
      } else if (OB_SUCCESS != ret && error.empty()) {
        error = "plugin verification failed";
      }
    }
    if (OB_SUCCESS == ret) {
      ret = canonical_existing(module->verified_artifact_->load_path(), false,
                               module->canonical_path_, error);
      if (OB_SUCCESS == ret && !contains_path(trusted_directory, module->canonical_path_)) {
        ret = OB_INVALID_ARGUMENT;
        error = "verified artifact resolves outside the trusted directory";
      }
    }
    if (OB_SUCCESS == ret) {
      const ObPluginArtifactMetadata &expected =
          module->verified_artifact_->metadata();
      if (!valid_identifier(expected.plugin_id_) ||
          !valid_bounded_text(expected.build_id_,
                              SEEKDB_PLUGIN_MAX_BUILD_ID_BYTES) ||
          !valid_digest(expected.package_digest_) ||
          0 == expected.package_version_.major) {
        ret = OB_INVALID_DATA;
        error = "verified artifact activation metadata is incomplete";
      } else if (nullptr != recovery &&
                 (recovery->plugin_id_ != expected.plugin_id_ ||
                  recovery->package_digest_ != expected.package_digest_)) {
        ret = OB_INVALID_DATA;
        error = "startup recovery artifact identity does not match catalog intent";
      } else {
        activation_request.mode_ = nullptr == recovery
            ? ObPluginActivationMode::ACTIVATE
            : ObPluginActivationMode::STARTUP_RECOVERY;
        activation_request.relative_path_ = relative_path;
        activation_request.plugin_id_ = expected.plugin_id_;
        activation_request.build_id_ = expected.build_id_;
        activation_request.package_digest_ = expected.package_digest_;
        activation_request.package_version_ = expected.package_version_;
        activation_request.catalog_version_ = expected.catalog_version_;
        activation_request.data_format_version_ = expected.data_format_version_;
        module->plugin_id_ = expected.plugin_id_;
        module->version_ = expected.package_version_;
        if (nullptr != recovery) {
          activation_request.expected_generation_ = recovery->generation_;
          activation_request.expected_runtime_incarnation_ =
              recovery->runtime_incarnation_;
          activation_request.expected_operation_id_ = recovery->operation_id_;
        }
      }
    }
    if (OB_SUCCESS == ret) {
      // Reject a locally fenced identity before creating another durable
      // activation intent.  In particular, UNKNOWN/BLOCKED/STOPPED runtimes
      // remain in active_ until recovery or terminal shutdown.  The check
      // after begin_activation remains as a defensive fence for the exact
      // catalog-assigned generation/incarnation/operation tuple.
      std::lock_guard<std::mutex> guard(impl_->mutex_);
      if (!impl_->initialized_ || impl_->shutting_down_ ||
          !impl_->loading_) {
        ret = OB_STATE_NOT_MATCH;
        error = "plugin loader entered terminal shutdown before activation begin";
      } else if (impl_->active_.count(activation_request.plugin_id_) != 0) {
        ret = OB_ENTRY_EXIST;
        error = "a generation of this plugin is already resident";
      } else {
        // Reserve both the success container and an identity placeholder
        // before catalog begin.  If a later permit abort is itself uncertain,
        // this preallocated record can fence the verified plugin identity even
        // when generation-owner allocation failed.
        impl_->modules_.reserve(impl_->modules_.size() + 1);
        const auto inserted = impl_->active_.insert(std::make_pair(
            module->plugin_id_, static_cast<Impl::Module *>(nullptr)));
        if (!inserted.second) {
          ret = OB_ENTRY_EXIST;
          error = "a generation of this plugin became resident concurrently";
        } else {
          active_placeholder = true;
        }
      }
    }
    if (OB_SUCCESS == ret) {
      activation_result.phase_ = ObPluginActivationPhase::CATALOG_BEGIN;
      try {
        ret = activation_guard->begin_activation(
            activation_request, activation_permit, error);
      } catch (const std::bad_alloc &) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        assign_error_noexcept(error, "activation guard allocation failed");
      } catch (...) {
        ret = OB_ERR_UNEXPECTED;
        assign_error_noexcept(error, "activation guard threw an exception");
      }
      if (OB_SUCCESS == ret && !activation_permit) {
        ret = OB_ERR_UNEXPECTED;
        error = "catalog coordinator returned no activation permit";
      } else if (OB_SUCCESS != ret) {
        if (error.empty()) {
          error = "catalog rejected plugin activation";
        }
        if (activation_permit) {
          // An unsuccessful begin never issues a usable permit.  Its
          // destructor owns any uncertain durable-begin recovery marker.
          activation_permit.reset();
        }
      }
    }
    if (OB_SUCCESS == ret) {
      permit_issued = true;
      const uint64_t generation = activation_permit->generation();
      const std::string &incarnation =
          activation_permit->runtime_incarnation();
      const std::string &operation_id = activation_permit->operation_id();
      activation_result.generation_ = generation;
      activation_result.runtime_incarnation_ = incarnation;
      activation_result.operation_id_ = operation_id;
      module->generation_ = generation;
      module->runtime_incarnation_ = incarnation;
      module->operation_id_ = operation_id;
      if (0 == generation || !valid_identifier(incarnation) ||
          !valid_identifier(operation_id) ||
          (nullptr != recovery &&
           (generation != recovery->generation_ ||
            incarnation != recovery->runtime_incarnation_ ||
            operation_id != recovery->operation_id_))) {
        ret = OB_INVALID_DATA;
        error = "catalog activation permit identity is invalid";
      }
    }
    if (OB_SUCCESS == ret) {
      std::lock_guard<std::mutex> guard(impl_->mutex_);
      if (!impl_->initialized_ || impl_->shutting_down_ || !impl_->loading_) {
        ret = OB_STATE_NOT_MATCH;
        error = "plugin loader entered terminal shutdown during activation begin";
      } else {
        const auto active_it = impl_->active_.find(module->plugin_id_);
        if (!active_placeholder || active_it == impl_->active_.end() ||
            nullptr != active_it->second) {
          ret = OB_ERR_UNEXPECTED;
          error = "plugin activation identity placeholder was lost";
        }
      }
      if (OB_SUCCESS == ret) {
        for (const std::unique_ptr<Impl::Module> &resident : impl_->modules_) {
          if (resident->plugin_id_ == module->plugin_id_ &&
              (resident->generation_ == module->generation_ ||
               resident->runtime_incarnation_ ==
                   module->runtime_incarnation_ ||
               resident->operation_id_ == module->operation_id_)) {
            ret = OB_ENTRY_EXIST;
            error = "catalog activation identity was already used in this runtime";
            break;
          }
        }
      }
      if (OB_SUCCESS == ret) {
        module->owner_.reset(new ObPluginGeneration(
            module->plugin_id_, module->generation_));
        activation_result.actual_state_ = module->owner_->state();
      }
    }
    if (OB_SUCCESS == ret) {
      activation_result.phase_ = ObPluginActivationPhase::LOADING;
      module->handle_ = open_module(module->canonical_path_, error);
      if (INVALID_MODULE == module->handle_)
        ret = OB_IO_ERROR;
    }
    if (OB_SUCCESS == ret) {
      ret = find_entry(module->handle_, entry, error);
    }
    if (OB_SUCCESS == ret) {
      try {
        module->manifest_ = entry();
      } catch (...) {
        ret = OB_ERR_UNEXPECTED;
        error = "plugin entry point threw an exception";
      }
    }
    if (OB_SUCCESS == ret) {
      ret = impl_->validate_manifest(module->manifest_, manifest_services, error);
      if (ret == OB_SUCCESS) module->server_dev_admitted_ =
          (module->manifest_->capabilities & SEEKDB_PLUGIN_CAPABILITY_SERVER_DEV) != 0;
    }
    if (OB_SUCCESS == ret) {
      const ObPluginArtifactMetadata &expected = module->verified_artifact_->metadata();
      if (expected.plugin_id_ != module->manifest_->plugin_id ||
          expected.build_id_ != module->manifest_->build_id ||
          !same_version(expected.package_version_, module->manifest_->version) ||
          expected.catalog_version_ != module->manifest_->catalog_version ||
          expected.data_format_version_ != module->manifest_->data_format_version) {
        ret = OB_INVALID_DATA;
        error = "verified artifact metadata does not match the binary manifest";
      }
    }
    if (OB_SUCCESS == ret &&
        OB_SUCCESS != (ret = module->owner_->transition_to(ObPluginState::VALIDATED))) {
      error = "failed to enter validated state";
    }
    if (OB_SUCCESS == ret &&
        OB_SUCCESS != (ret = module->owner_->transition_to(ObPluginState::LOADED))) {
      error = "failed to enter loaded state";
    }
    if (OB_SUCCESS == ret) {
      module->host_.registry_ = impl_->registry_.get();
      module->host_.owner_ = module->owner_;
      init_host_api(module->host_);
      ret = impl_->resolve_dependencies(*module, error, failure_reason);

      if (OB_SUCCESS == ret) {
        ret = impl_->registry_->begin_registration(module->owner_, publication);
        publication_open = OB_SUCCESS == ret;
        if (OB_SUCCESS != ret)
          error = "cannot begin atomic service publication";
      }
      for (size_t i = 0; OB_SUCCESS == ret && i < manifest_services.size(); ++i) {
        const StagedService &service = manifest_services[i];
        if (0 == (service.spec_.capabilities_ &
                  SEEKDB_PLUGIN_CAPABILITY_EXTENSION_CATALOG)) {
          ret = publication.add_service(
              service.spec_.name_.c_str(), service.spec_.abi_major_,
              service.spec_.abi_minor_, service.spec_.abi_patch_,
              service.spec_.capabilities_, service.spec_.service_);
        }
        if (OB_SUCCESS != ret)
          error = "manifest service conflicts with the registry";
      }
      if (OB_SUCCESS == ret) {
        ret = module->owner_->transition_to(ObPluginState::INITIALIZING);
        if (OB_SUCCESS != ret)
          error = "failed to enter initializing state";
      }
      if (OB_SUCCESS == ret) {
        activation_result.phase_ = ObPluginActivationPhase::INITIALIZING;
        {
          std::lock_guard<std::mutex> host_guard(module->host_.mutex_);
          ret = from_plugin_status(registration_status(
              seekdb_runtime_registration_open(module->host_.registration_.get())));
        }
        if (OB_SUCCESS == ret) {
          ret = call_lifecycle_init(module->manifest_->init, &module->host_.api_.v2.host, &module->instance_);
        }
        module->initialized_ = OB_SUCCESS == ret;
        if (OB_SUCCESS != ret || nullptr == module->instance_) {
          if (OB_SUCCESS == ret)
            ret = OB_INVALID_DATA;
          error = "plugin init callback failed or returned no instance";
        }
      }
      if (OB_SUCCESS == ret) {
        // Once start has been entered, rollback must call stop even when start
        // reports failure: the plugin may have started only part of its work.
        activation_result.phase_ = ObPluginActivationPhase::STARTING;
        activation_result.start_entered_ = true;
        module->started_ = true;
        ret = call_lifecycle(module->manifest_->start, module->instance_);
        if (OB_SUCCESS != ret)
          error = "plugin start callback failed";
      }
      {
        std::lock_guard<std::mutex> host_guard(module->host_.mutex_);
        const int32_t seal_status = seekdb_runtime_registration_seal(module->host_.registration_.get());
        if (OB_SUCCESS == ret && SEEKDB_RUNTIME_OK != seal_status) {
          ret = OB_STATE_NOT_MATCH;
          error = "plugin left a registration transaction open";
        }
        if (OB_SUCCESS == ret) ret = collect_registered_objects(module->host_);
        if (OB_SUCCESS == ret &&
                   (module->host_.staged_.size() > MAX_SERVICE_COUNT ||
                    manifest_services.size() >
                        MAX_SERVICE_COUNT - module->host_.staged_.size())) {
          ret = OB_SIZE_OVERFLOW;
          error = "combined manifest and dynamic service count is invalid";
        }
      }
      for (size_t i = 0; OB_SUCCESS == ret && i < module->host_.staged_.size(); ++i) {
        const StagedService &service = module->host_.staged_[i];
        if (0 == (service.spec_.capabilities_ &
                  SEEKDB_PLUGIN_CAPABILITY_EXTENSION_CATALOG)) {
          ret = publication.add_service(
              service.spec_.name_.c_str(), service.spec_.abi_major_,
              service.spec_.abi_minor_, service.spec_.abi_patch_,
              service.spec_.capabilities_, service.spec_.service_);
        }
        if (OB_SUCCESS != ret)
          error = "dynamic service conflicts with manifest or registry";
      }
      if (OB_SUCCESS == ret) {
        ExtensionManifestRequirements requirements;
        for (const ObPluginExtensionSpec &extension : module->host_.staged_extensions_) {
          requirements.observe(extension);
          ret = publication.add_extension(extension);
          if (OB_SUCCESS != ret) {
            error = "directly registered extension conflicts with registry: " + extension.object_id_;
            break;
          }
        }
        if (OB_SUCCESS == ret) {
          ret = validate_extension_requirements(requirements, *module->manifest_, error);
        }
      }
      if (OB_SUCCESS == ret) {
        activation_result.phase_ = ObPluginActivationPhase::DISCOVERING;
        ret = discover_and_stage_extensions(
            module->instance_, *module->manifest_, manifest_services,
            module->host_.staged_, publication, error);
      }
      if (OB_SUCCESS == ret) {
        activation_result.phase_ =
            ObPluginActivationPhase::PREPARING_CANDIDATE;
        ret = publication.prepare(candidate);
        if (OB_SUCCESS == ret) {
          publication_open = false;
          activation_result.candidate_prepared_ = true;
          activation_result.candidate_base_epoch_ = candidate.base_epoch();
          // These copies are the last potentially allocating work before the
          // catalog transaction.  Catalog code never observes DSO-owned
          // descriptor memory and promote remains allocation-free.
          activation_result.services_ = candidate.contributed_services();
          activation_result.extensions_ = candidate.contributed_extensions();
          activation_result.dependencies_ = module->resolved_dependencies_;
          module->host_.staged_.clear();
          module->host_.staged_extensions_.clear();
          {
            std::lock_guard<std::mutex> host_guard(module->host_.mutex_);
            (void)seekdb_runtime_registration_clear(module->host_.registration_.get());
          }
        } else {
          error = "atomic service candidate preparation failed";
        }
      }
      if (OB_SUCCESS == ret) {
        // This mutex acquisition is the activation/shutdown linearization
        // point.  If shutdown won, the invisible candidate is rolled back.  If
        // activation wins, a later shutdown observes loading_ and must retry;
        // catalog commit is then always followed by no-fail promotion.
        std::lock_guard<std::mutex> guard(impl_->mutex_);
        if (!impl_->initialized_ || impl_->shutting_down_ ||
            !impl_->loading_) {
          ret = OB_STATE_NOT_MATCH;
          error = "plugin loader entered terminal shutdown before catalog commit";
        } else {
          activation_result.status_ = OB_SUCCESS;
          activation_result.actual_state_ = module->owner_->state();
          activation_result.phase_ = ObPluginActivationPhase::CATALOG_FINISH;
          activation_result.error_.clear();
        }
      }
      if (OB_SUCCESS == ret) {
        commit_attempted = true;
        activation_decision = OB_PLUGIN_ACTIVATION_UNKNOWN;
        catalog_error.clear();
        const int commit_ret = activation_permit->commit_candidate(
            activation_result, activation_decision, activation_commit,
            catalog_error);

        if (OB_PLUGIN_ACTIVATION_PROMOTE == activation_decision &&
            OB_SUCCESS == commit_ret && activation_commit) {
          catalog_committed = true;
        } else if (OB_PLUGIN_ACTIVATION_NOT_COMMITTED ==
                       activation_decision &&
                   !activation_commit) {
          ret = OB_SUCCESS == commit_ret ? OB_STATE_NOT_MATCH : commit_ret;
          if (!catalog_error.empty()) {
            error = catalog_error;
          } else {
            error = "catalog did not authorize plugin activation";
          }
        } else {
          // UNKNOWN, or any contradictory return/token tuple, cannot prove
          // that ownership rows were not committed.  Never publish or issue a
          // normal abort; retain the identity for startup recovery.
          activation_decision = OB_PLUGIN_ACTIVATION_UNKNOWN;
          identity_must_remain = true;
          ret = OB_TRANS_UNKNOWN;
          if (!catalog_error.empty()) {
            error = catalog_error;
          } else {
            error = "plugin activation catalog outcome is unknown";
          }
        }
      }
      if (OB_SUCCESS == ret && catalog_committed) {
        // From this point onward there is no business rollback path.  The
        // candidate reservation made promotion infallible, and every loader
        // container needed below was preallocated before dlopen.  Even an
        // impossible mutex/bookkeeping exception is fail-stop: rolling back a
        // durably committed catalog activation would create split brain.
        try {
          activation_result.phase_ = ObPluginActivationPhase::PROMOTING;
          std::lock_guard<std::mutex> guard(impl_->mutex_);
          if (SEEKDB_RUNTIME_OK != seekdb_runtime_native_publish(module->handle_)) {
            std::terminate();
          }
          candidate.promote();
          promoted = true;
          activation_result.actual_state_ = ObPluginState::ACTIVE;
          activation_result.phase_ = ObPluginActivationPhase::COMPLETE;
          activation_result.status_ = OB_SUCCESS;
          activation_result.error_.clear();
          promoted_module = module.get();
          impl_->active_.find(module->plugin_id_)->second = promoted_module;
          if (nullptr != loaded_generation)
            *loaded_generation = module->generation_;
          impl_->modules_.push_back(std::move(module));
        } catch (...) {
          std::terminate();
        }
      }
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    assign_error_noexcept(error, "plugin load allocation failed");
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
    assign_error_noexcept(error, "unexpected exception during plugin load");
  }

  if (promoted) {
    // complete() records ACTIVE and clears PROMOTE_PENDING.  A failure here
    // leaves a replayable catalog intent but must not roll back live runtime.
    catalog_error.clear();
    const int complete_ret = activation_commit->complete(
        activation_result, catalog_error);
    if (OB_SUCCESS != complete_ret) {
      ret = complete_ret;
      if (!catalog_error.empty()) {
        assign_error_noexcept(error, catalog_error.c_str());
      } else {
        assign_error_noexcept(
            error, "catalog failed to finalize active plugin runtime");
      }
    } else {
      ret = OB_SUCCESS;
      error.clear();
    }
    try {
      std::lock_guard<std::mutex> guard(impl_->mutex_);
      if (nullptr != promoted_module) {
        assign_error_noexcept(promoted_module->last_error_, error.c_str());
      }
      if (load_reserved) {
        impl_->loading_ = false;
        load_reserved = false;
      }
      impl_->set_error(error);
      impl_->last_failure_reason_ = OB_SUCCESS == ret
          ? ObPluginLoadFailureReason::NONE
          : (ObPluginLoadFailureReason::REQUIRED_SERVICE_UNAVAILABLE ==
                     failure_reason
                 ? failure_reason
                 : ObPluginLoadFailureReason::OTHER);
    } catch (...) {
      // Runtime is already ACTIVE and catalog is committed.  Failing closed is
      // safer than unwinding into a caller that might attempt compensating
      // rollback or reuse the in-flight identity.
      std::terminate();
    }
  } else if (OB_SUCCESS != ret) {
    // Releasing the hidden registry reservation must precede FAILED/BLOCKED
    // transitions; the generation deliberately rejects lifecycle mutations
    // while a candidate is prepared.
    candidate.abort();
    if (publication_open) {
      publication.rollback();
      publication_open = false;
    }
    bool safe_to_teardown = true;
    if (module && module->started_) {
      const int stop_ret = call_lifecycle(module->manifest_->stop, module->instance_);
      if (OB_SUCCESS == stop_ret) {
        module->started_ = false;
      } else {
        safe_to_teardown = false;
        append_error_noexcept(error, "; rollback stop failed and the module remains mapped");
      }
    }
    if (module && safe_to_teardown && nullptr != module->instance_) {
      call_deinit(module->manifest_->deinit, module->instance_);
      module->initialized_ = false;
      module->instance_ = nullptr;
    }
    if (module) {
      if (safe_to_teardown) {
        cleanup_host_resources(module->host_);
        impl_->release_dependencies(*module);
      }
      if (module->owner_) {
        if (safe_to_teardown) {
          impl_->fail_generation(*module, error);
        } else {
          impl_->block_generation(*module, error);
        }
      }
      if (safe_to_teardown && module->handle_ != INVALID_MODULE) {
        std::string close_error;
        if (OB_SUCCESS == close_module(module->handle_, SEEKDB_RUNTIME_ABORT_LOAD, close_error)) {
          module->handle_ = INVALID_MODULE;
          module->manifest_ = nullptr;
        } else {
          // Retain both the mapping and verified artifact until terminal retry.
          // Do not admit another instance while old static state is resident.
          identity_must_remain = true;
          append_error_noexcept(error, "; native module close failed: ");
          append_error_noexcept(error, close_error.c_str());
        }
      }
    }

    if (!safe_to_teardown) {
      identity_must_remain = true;
    }
    activation_result.status_ = ret;
    activation_result.actual_state_ =
        module && module->owner_ ? module->owner_->state()
                                 : ObPluginState::DISCOVERED;
    assign_error_noexcept(activation_result.error_, error.c_str());

    if (permit_issued &&
        (!commit_attempted ||
         OB_PLUGIN_ACTIVATION_NOT_COMMITTED == activation_decision)) {
      catalog_error.clear();
      const int abort_ret = activation_permit->abort(
          activation_result, catalog_error);
      if (OB_SUCCESS != abort_ret) {
        identity_must_remain = true;
        ret = abort_ret;
        if (!catalog_error.empty()) {
          append_error_noexcept(error, "; catalog activation abort failed: ");
          append_error_noexcept(error, catalog_error.c_str());
        } else {
          append_error_noexcept(error, "; catalog activation abort failed");
        }
      }
    }

    {
      std::lock_guard<std::mutex> guard(impl_->mutex_);
      if (module && active_placeholder) {
        if (safe_to_teardown && !identity_must_remain) {
          impl_->active_.erase(module->plugin_id_);
        } else {
          // BLOCKED and catalog-uncertain identities remain occupied until
          // terminal shutdown/recovery so a second runtime cannot start.
          impl_->active_.find(module->plugin_id_)->second = module.get();
        }
      }
      if (module &&
          (module->owner_ || identity_must_remain || !safe_to_teardown)) {
        assign_error_noexcept(module->last_error_, error.c_str());
        impl_->modules_.push_back(std::move(module));
      }
      if (load_reserved) {
        impl_->loading_ = false;
        load_reserved = false;
      }
      impl_->set_error(error);
      impl_->last_failure_reason_ =
          ObPluginLoadFailureReason::REQUIRED_SERVICE_UNAVAILABLE ==
                  failure_reason
              ? failure_reason
              : ObPluginLoadFailureReason::OTHER;
    }
  }
  return ret;
}

int ObPluginLoader::disable(const std::string &plugin_id, const int64_t drain_timeout_us)
{
  if (!impl_) return OB_ALLOCATE_MEMORY_FAILED;
  int ret = OB_SUCCESS;
  std::string error;
  std::shared_ptr<const ObPluginDisableGuard> disable_guard;
  Impl::Module *module = nullptr;
  uint64_t expected_generation = 0;
  bool reserved = false;

  // Reserve the exact generation before entering the catalog protocol.  The
  // reservation is logical (not a held mutex): shutdown will fail retryably,
  // loads are rejected, and plugin/catalog callbacks never run under mutex_.
  {
    std::lock_guard<std::mutex> guard(impl_->mutex_);
    if (!impl_->initialized_) {
      ret = OB_NOT_INIT;
      error = "plugin loader is not initialized";
    } else if (plugin_id.empty() || drain_timeout_us < 0) {
      ret = OB_INVALID_ARGUMENT;
      error = "plugin id and drain timeout are invalid";
    } else if (impl_->shutting_down_) {
      ret = OB_STATE_NOT_MATCH;
      error = "plugin loader is shutting down";
    } else if (impl_->loading_) {
      ret = OB_EAGAIN;
      error = "plugin load operation is in progress";
    } else if (impl_->disabling_.count(plugin_id) != 0) {
      ret = OB_EAGAIN;
      error = "plugin already has a disable operation in progress";
    } else {
      const auto it = impl_->active_.find(plugin_id);
      if (it == impl_->active_.end() || nullptr == it->second) {
        ret = OB_ENTRY_NOT_EXIST;
        error = "resident plugin was not found";
      } else if (!it->second->owner_ ||
                 (it->second->owner_->state() != ObPluginState::ACTIVE &&
                  it->second->owner_->state() != ObPluginState::QUIESCING)) {
        ret = OB_STATE_NOT_MATCH;
        error = "plugin generation cannot be disabled at runtime";
      } else {
        module = it->second;
        expected_generation = it->second->generation_;
        disable_guard = impl_->disable_guard_;
        try {
          reserved = impl_->disabling_.insert(
              std::make_pair(plugin_id, expected_generation)).second;
          if (!reserved) {
            ret = OB_EAGAIN;
            error = "plugin already has a disable operation in progress";
          }
        } catch (const std::bad_alloc &) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          error = "cannot reserve plugin disable operation";
        } catch (...) {
          ret = OB_ERR_UNEXPECTED;
          error = "cannot reserve plugin disable operation";
        }
      }
    }
    if (OB_SUCCESS != ret) impl_->set_error(error);
  }

  std::unique_ptr<ObPluginDisablePermit> permit;
  if (OB_SUCCESS == ret) {
    try {
      ret = disable_guard->begin_restricted_disable(
          plugin_id, expected_generation, permit, error);
    } catch (const std::bad_alloc &) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      assign_error_noexcept(error, "disable guard allocation failed");
    } catch (...) {
      ret = OB_ERR_UNEXPECTED;
      assign_error_noexcept(error, "disable guard threw an exception");
    }
    if (OB_SUCCESS == ret && !permit) {
      ret = OB_ERR_UNEXPECTED;
      assign_error_noexcept(error,
                            "catalog coordinator returned no disable permit");
    } else if (OB_SUCCESS != ret && error.empty()) {
      assign_error_noexcept(error, "catalog rejected plugin disable");
    }
    if (OB_SUCCESS != ret && permit) {
      // begin() failed, so the permit was never issued.  Its destructor owns
      // abort/recovery; finish() is reserved for a successfully issued permit.
      permit.reset();
    }
  }

  ObPluginRuntimeDisableResult runtime_result;
  runtime_result.generation_ = expected_generation;
  if (nullptr != module && module->owner_) {
    runtime_result.actual_state_ = module->owner_->state();
  }
  std::string runtime_error;
  bool stop_checkpoint_failed = false;
  if (OB_SUCCESS == ret) {
    bool may_run = false;
    {
      std::lock_guard<std::mutex> guard(impl_->mutex_);
      const auto active_it = impl_->active_.find(plugin_id);
      const auto disabling_it = impl_->disabling_.find(plugin_id);
      if (!impl_->initialized_ || impl_->shutting_down_) {
        runtime_result.status_ = OB_STATE_NOT_MATCH;
        assign_error_noexcept(runtime_error, "plugin loader is shutting down");
      } else if (active_it == impl_->active_.end() || active_it->second != module ||
                 nullptr == module || module->generation_ != expected_generation ||
                 disabling_it == impl_->disabling_.end() ||
                 disabling_it->second != expected_generation) {
        runtime_result.status_ = OB_STATE_NOT_MATCH;
        assign_error_noexcept(runtime_error,
                              "plugin generation changed before runtime disable");
      } else {
        may_run = true;
      }
    }
    if (may_run) {
      (void)impl_->disable_runtime(
          *module, drain_timeout_us, false, permit.get(),
          stop_checkpoint_failed, nullptr, runtime_result, runtime_error);
    } else if (nullptr != module && module->owner_) {
      runtime_result.actual_state_ = module->owner_->state();
      assign_error_noexcept(runtime_result.error_, runtime_error.c_str());
    }
  }

  int finish_ret = OB_SUCCESS;
  if (permit && stop_checkpoint_failed) {
    // The catalog could not prove whether the pre-stop checkpoint committed.
    // The callback was not entered.  Do not overwrite a possibly durable
    // stop_entered marker with a weaker finish observation; permit destruction
    // records RECOVERY_REQUIRED while preserving the checkpoint columns.
    finish_ret = runtime_result.status_;
    ret = finish_ret;
    error.clear();
    if (!runtime_result.error_.empty()) {
      assign_error_noexcept(error, runtime_result.error_.c_str());
    }
    permit.reset();
  } else if (permit) {
    std::string catalog_error;
    try {
      finish_ret = permit->finish(runtime_result, catalog_error);
    } catch (const std::bad_alloc &) {
      finish_ret = OB_ALLOCATE_MEMORY_FAILED;
      assign_error_noexcept(catalog_error,
                            "disable permit finalization allocation failed");
    } catch (...) {
      finish_ret = OB_ERR_UNEXPECTED;
      assign_error_noexcept(catalog_error,
                            "disable permit finalization threw an exception");
    }
    ret = OB_SUCCESS == finish_ret ? runtime_result.status_ : finish_ret;
    error.clear();
    if (!runtime_result.error_.empty()) {
      assign_error_noexcept(error, runtime_result.error_.c_str());
    }
    if (OB_SUCCESS != finish_ret) {
      if (!error.empty()) {
        append_error_noexcept(error, "; catalog finalization failed: ");
      }
      append_error_noexcept(
          error, catalog_error.empty() ? "catalog outcome requires recovery"
                                       : catalog_error.c_str());
    }
  }

  {
    std::lock_guard<std::mutex> guard(impl_->mutex_);
    if (reserved) {
      const auto it = impl_->disabling_.find(plugin_id);
      if (it != impl_->disabling_.end() && it->second == expected_generation) {
        impl_->disabling_.erase(it);
      }
    }
    if (nullptr != module) {
      try {
        if (OB_SUCCESS == ret) {
          module->last_error_.clear();
          // A successfully stopped generation is no longer resident from the
          // management API's perspective.  Keep its immutable Module record
          // in modules_ for generation/audit history, but remove it from the
          // active index so a later INSTALL PLUGIN can create a new fenced
          // generation after UNINSTALL PLUGIN.
          if (module->owner_ &&
              module->owner_->state() == ObPluginState::STOPPED) {
            const auto active_it = impl_->active_.find(plugin_id);
            if (active_it != impl_->active_.end() &&
                active_it->second == module) {
              impl_->active_.erase(active_it);
            }
          }
        } else {
          module->last_error_ = error;
        }
      } catch (...) {
        module->last_error_.clear();
      }
    }
    if (OB_SUCCESS == ret) {
      impl_->set_error(std::string());
    } else if (!error.empty()) {
      impl_->set_error(error);
    } else if (OB_SUCCESS != finish_ret) {
      impl_->set_error("catalog failed to persist the runtime disable result");
    } else {
      impl_->set_error("runtime plugin disable failed");
    }
  }
  return ret;
}

int ObPluginLoader::shutdown_for_process_exit(const int64_t drain_timeout_us)
{
  if (!impl_) return OB_ALLOCATE_MEMORY_FAILED;
  int ret = OB_SUCCESS;
  bool shutdown_reserved = false;
  const ObPluginTerminalStopAuthority terminal_authority;
  {
    std::lock_guard<std::mutex> guard(impl_->mutex_);
    if (!impl_->initialized_) {
      ret = OB_NOT_INIT;
      impl_->set_error("plugin loader is not initialized");
    } else if (drain_timeout_us < 0) {
      ret = OB_INVALID_ARGUMENT;
      impl_->set_error("drain timeout is invalid");
    } else if (impl_->shutdown_running_) {
      ret = OB_EAGAIN;
      impl_->set_error("terminal plugin shutdown is already in progress");
    } else {
      // This is a terminal request even when it has to be retried.  Prevent new
      // loads/disables immediately, but never race a catalog permit already in
      // flight or wait for it while running inside a coordinator callback.
      impl_->shutting_down_ = true;
      if (impl_->loading_ || !impl_->disabling_.empty()) {
        ret = OB_EAGAIN;
        impl_->set_error(
            "plugin management operation is in progress; retry process shutdown");
      } else {
        impl_->shutdown_running_ = true;
        shutdown_reserved = true;
      }
    }
  }

  for (size_t index = impl_->modules_.size(); OB_SUCCESS == ret && index > 0; --index) {
    Impl::Module &module = *impl_->modules_[index - 1];
    const ObPluginState state = module.owner_ ? module.owner_->state()
                                              : ObPluginState::FAILED;
    if (module.owner_ &&
        (ObPluginState::ACTIVE == state || ObPluginState::QUIESCING == state ||
         ObPluginState::BLOCKED == state)) {
      ObPluginRuntimeDisableResult runtime_result;
      std::string error;
      bool stop_checkpoint_failed = false;
      ret = impl_->disable_runtime(
          module, drain_timeout_us, true, nullptr, stop_checkpoint_failed,
          &terminal_authority, runtime_result, error);
      std::lock_guard<std::mutex> guard(impl_->mutex_);
      try {
        if (OB_SUCCESS == ret) {
          module.last_error_.clear();
          impl_->active_.erase(module.plugin_id_);
        } else {
          module.last_error_ = error;
        }
      } catch (...) {
        module.last_error_.clear();
      }
      if (OB_SUCCESS != ret) impl_->set_error(error);
    }
  }

  if (OB_SUCCESS == ret) {
    // Mark handles unavailable under the mutex, but invoke platform unload and
    // plugin static destructors without it.  This API is terminal-only, so no
    // new work can observe the transient state.
    for (size_t index = 0; OB_SUCCESS == ret && index < impl_->modules_.size(); ++index) {
      ModuleHandle handle = INVALID_MODULE;
      {
        std::lock_guard<std::mutex> guard(impl_->mutex_);
        Impl::Module &module = *impl_->modules_[index];
        handle = module.handle_;
        module.handle_ = INVALID_MODULE;
        module.manifest_ = nullptr;
      }
      if (INVALID_MODULE != handle) {
        std::string close_error;
        ret = close_module(handle, SEEKDB_RUNTIME_PROCESS_EXIT, close_error);
        if (OB_SUCCESS != ret) {
          std::lock_guard<std::mutex> guard(impl_->mutex_);
          Impl::Module &module = *impl_->modules_[index];
          module.handle_ = handle;
          assign_error_noexcept(module.last_error_, close_error.c_str());
          impl_->set_error(close_error);
        }
      }
    }
  }
  if (OB_SUCCESS == ret) {
    std::lock_guard<std::mutex> guard(impl_->mutex_);
    impl_->active_.clear();
    impl_->initialized_ = false;
    impl_->terminal_completed_ = true;
    impl_->shutdown_running_ = false;
    shutdown_reserved = false;
    impl_->set_error(std::string());
  }
  if (shutdown_reserved) {
    std::lock_guard<std::mutex> guard(impl_->mutex_);
    impl_->shutdown_running_ = false;
  }
  return ret;
}

namespace {
using CatalogBuildCallback = decltype(seekdb_plugin_catalog_service_v2_t::build);
int validate_catalog_service(const seekdb_plugin_catalog_service_v1_t *service, CatalogBuildCallback &build)
{
  build = nullptr;
  if (!service || service->struct_size < sizeof(*service) || service->spi_major != 1 || service->spi_minor > 1 ||
      service->reserved_word || !service->prepare || !all_zero(service->reserved, 4)) return OB_NOT_SUPPORTED;
  if (service->spi_minor == 1) {
    if (service->struct_size < sizeof(seekdb_plugin_catalog_service_v2_t)) return OB_NOT_SUPPORTED;
    const auto *extended = reinterpret_cast<const seekdb_plugin_catalog_service_v2_t *>(service);
    if (!extended->build || !all_zero(extended->reserved, 4)) return OB_NOT_SUPPORTED;
    build = extended->build;
  }
  return OB_SUCCESS;
}

struct CatalogDeclarations final : ICatalogDeclarations, ICatalogBuildProgram {
  ObPluginLease lease;
  std::vector<std::string> statements;
  const std::thread::id thread = std::this_thread::get_id();
  std::string name, version, module;
  size_t bytes = 0;
  int error = OB_SUCCESS;
  bool closed = false;
  bool built = false;
  uint64_t tenant_id = 0, database_id = 0, owner_id = 0;
  seekdb_plugin_instance_handle_t *instance = nullptr;
  decltype(seekdb_plugin_catalog_service_v2_t::build) build_callback = nullptr;
  const std::vector<std::string> &sql() const override { return statements; }
  ICatalogBuildProgram *program() override { return build_callback ? this : nullptr; }
  int preflight(const ExtensionInstallSpec &spec, std::string &) override {
    return closed && !built && build_callback && spec.tenant_id_ == tenant_id &&
        spec.database_id_ == database_id && spec.owner_id_ == owner_id && spec.name_ == name &&
        spec.version_ == version && spec.native_module_id_ == module ? OB_SUCCESS : OB_STATE_NOT_MATCH;
  }
  int build(ICatalogRoutineBuilder &target, std::string &diagnostic) override {
    if (!closed || built || !build_callback || !instance) return OB_STATE_NOT_MATCH;
    built = true;
    struct Bridge {
      ICatalogRoutineBuilder &target;
      std::string &diagnostic;
      const std::thread::id thread = std::this_thread::get_id();
      int error = OB_SUCCESS;
      static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL lookup(void *opaque, uint32_t kind,
          const char *name, uint64_t size, uint64_t *id) noexcept {
        if (id) *id = 0;
        if (!opaque) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
        auto &self = *static_cast<Bridge *>(opaque);
        if (self.thread != std::this_thread::get_id()) return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
        if (self.error != OB_SUCCESS) return to_plugin_status(self.error);
        try {
          if (!id || !name || size == 0 || size > SEEKDB_PLUGIN_CATALOG_MAX_ROUTINE_NAME_BYTES ||
              (kind != SEEKDB_PLUGIN_CATALOG_ROUTINE_FUNCTION && kind != SEEKDB_PLUGIN_CATALOG_ROUTINE_PROCEDURE))
            self.error = OB_INVALID_ARGUMENT;
          else self.error = self.target.lookup_routine(static_cast<CatalogRoutineKind>(kind),
              std::string(name, static_cast<size_t>(size)), *id, self.diagnostic);
          if (self.error == OB_SUCCESS && *id > INT64_MAX) self.error = OB_ERR_UNEXPECTED;
        } catch (const std::bad_alloc &) { self.error = OB_ALLOCATE_MEMORY_FAILED;
        } catch (...) { self.error = OB_ERR_UNEXPECTED; }
        if (self.error != OB_SUCCESS && id) *id = 0;
        return to_plugin_status(self.error);
      }
      static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL create(void *opaque, const char *sql, uint64_t size, uint64_t *id) noexcept {
        if (id) *id = 0;
        if (!opaque) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
        auto &self = *static_cast<Bridge *>(opaque);
        if (self.thread != std::this_thread::get_id()) return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
        if (self.error != OB_SUCCESS) return to_plugin_status(self.error);
        try {
          if (!id || !sql || size == 0 || size > 4 * 1024 * 1024) self.error = OB_INVALID_ARGUMENT;
          else self.error = self.target.create_routine(std::string(sql, static_cast<size_t>(size)), *id, self.diagnostic);
          if (self.error == OB_SUCCESS && (*id == 0 || *id > INT64_MAX)) self.error = OB_ERR_UNEXPECTED;
        } catch (const std::bad_alloc &) { self.error = OB_ALLOCATE_MEMORY_FAILED;
        } catch (...) { self.error = OB_ERR_UNEXPECTED; }
        if (self.error != OB_SUCCESS && id) *id = 0;
        return to_plugin_status(self.error);
      }
    } bridge{target, diagnostic};
    const seekdb_plugin_catalog_build_context_v2_t context{{sizeof(context), 0, tenant_id, database_id, owner_id,
        name.c_str(), version.c_str(), &bridge, Bridge::create, {0}}, Bridge::lookup, {0}};
    try {
      const auto status = build_callback(instance, &context.v1);
      if (bridge.error != OB_SUCCESS) return bridge.error;
      if (status == SEEKDB_PLUGIN_STATUS_END_OF_STREAM) return OB_INVALID_ARGUMENT;
      return from_plugin_status(status);
    } catch (const std::bad_alloc &) { return bridge.error != OB_SUCCESS ? bridge.error : OB_ALLOCATE_MEMORY_FAILED;
    } catch (...) { return bridge.error != OB_SUCCESS ? bridge.error : OB_ERR_UNEXPECTED; }
  }
  static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit(void *opaque, const char *sql, uint64_t size) noexcept {
    if (!opaque) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    auto &self = *static_cast<CatalogDeclarations *>(opaque);
    if (self.thread != std::this_thread::get_id()) return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
    if (self.error != OB_SUCCESS) return to_plugin_status(self.error);
    try {
      if (self.closed || !sql || size == 0 || size > 4 * 1024 * 1024 - self.bytes || self.statements.size() >= 4096) {
        self.error = OB_INVALID_ARGUMENT;
      } else {
        // Reuse Rust's bounded, UTF-8/NUL-checked source constructor; this
        // validates source data only and never parses or executes SQL.
        const auto text = [](const std::string &s) {
          return seekdb_runtime_package_input_text{reinterpret_cast<const uint8_t *>(s.data()), static_cast<uint32_t>(s.size())};
        };
        const seekdb_runtime_package_input_script script{{nullptr, 0}, text(self.version),
            {reinterpret_cast<const uint8_t *>(sql), static_cast<uint32_t>(size)}};
        const seekdb_runtime_package_input_source input{sizeof(input), 0, text(self.name), {nullptr, 0},
            text(self.version), text(self.module), {nullptr, 0}, nullptr, 0, &script, 1, 0, nullptr, 0};
        seekdb_runtime_package *raw = nullptr;
        char diagnostic[128] = {};
        const int status = seekdb_runtime_package_from_source(&input, &raw, diagnostic, sizeof(diagnostic));
        std::unique_ptr<seekdb_runtime_package, decltype(&seekdb_runtime_package_destroy)> owned(raw, seekdb_runtime_package_destroy);
        if (status != SEEKDB_RUNTIME_OK || !owned) self.error = status == SEEKDB_RUNTIME_NO_MEMORY ? OB_ALLOCATE_MEMORY_FAILED : OB_INVALID_ARGUMENT;
        else {
          self.statements.emplace_back(sql, static_cast<size_t>(size));
          self.bytes += size;
        }
      }
    } catch (const std::bad_alloc &) { self.error = OB_ALLOCATE_MEMORY_FAILED;
    } catch (...) { self.error = OB_ERR_UNEXPECTED; }
    return to_plugin_status(self.error);
  }
};
} // namespace

int ObPluginLoader::prepare_catalog_install(const ExtensionPackageSource &source, uint64_t tenant_id,
    uint64_t database_id, uint64_t owner_id, std::unique_ptr<ICatalogDeclarations> &output)
try {
  output.reset();
  if (!impl_ || !impl_->registry_) return OB_NOT_INIT;
  if (tenant_id != 1 || database_id == 0 || database_id > INT64_MAX || owner_id == 0 || owner_id > INT64_MAX ||
      !source.from_version_.empty() || source.name_.empty() || source.name_.size() > 255 ||
      source.version_.empty() || source.version_.size() > 255 || source.native_module_.size() > 255 ||
      source.scripts_.size() > 1024 || source.name_.find('\0') != std::string::npos ||
      source.version_.find('\0') != std::string::npos || source.native_module_.find('\0') != std::string::npos)
    return OB_INVALID_ARGUMENT;
  if (source.native_install_ && (source.native_module_.empty() || !source.scripts_.empty())) return OB_INVALID_ARGUMENT;
  if (source.native_module_.empty()) return OB_SUCCESS;
  const std::string service_id = source.native_module_ + SEEKDB_PLUGIN_CATALOG_INSTALL_SUFFIX;
  // A module whose ID leaves no room for this optional service cannot advertise
  // it under the current registry ID bound; preserve ordinary SQL installation.
  if (service_id.size() > 255) return source.native_install_ ? OB_NOT_SUPPORTED : OB_SUCCESS;
  auto declarations = std::make_unique<CatalogDeclarations>();
  int ret = impl_->registry_->acquire(service_id.c_str(), 1, 0, declarations->lease);
  if (ret == OB_ENTRY_NOT_EXIST) return source.native_install_ ? OB_ENTRY_NOT_EXIST : OB_SUCCESS;
  if (ret != OB_SUCCESS) return ret;
  if (!declarations->lease.owner_plugin_id() || source.native_module_ != declarations->lease.owner_plugin_id()) return OB_STATE_NOT_MATCH;
  const auto *service = static_cast<const seekdb_plugin_catalog_service_v1_t *>(declarations->lease.service());
  ret = validate_catalog_service(service, declarations->build_callback);
  if (ret != OB_SUCCESS) return ret;
  auto *instance = impl_->instance_for_lease(declarations->lease);
  if (!instance) return OB_STATE_NOT_MATCH;
  declarations->instance = instance;
  declarations->tenant_id = tenant_id; declarations->database_id = database_id; declarations->owner_id = owner_id;
  for (const auto &script : source.scripts_) {
    if (script.sql_.size() > 4 * 1024 * 1024 - declarations->bytes) return OB_SIZE_OVERFLOW;
    declarations->bytes += script.sql_.size();
  }
  declarations->name = source.name_; declarations->version = source.version_; declarations->module = source.native_module_;
  const seekdb_plugin_catalog_context_v1_t context{sizeof(context), 0, tenant_id, database_id, owner_id,
      declarations->name.c_str(), declarations->version.c_str(), declarations.get(), CatalogDeclarations::emit, {0}};
  const auto status = service->prepare(instance, &context);
  declarations->closed = true;
  if (declarations->error != OB_SUCCESS) return declarations->error;
  if (status == SEEKDB_PLUGIN_STATUS_END_OF_STREAM) return OB_INVALID_ARGUMENT;
  if (status != SEEKDB_PLUGIN_STATUS_OK) return from_plugin_status(status);
  if (source.native_install_ && declarations->statements.empty() && !declarations->build_callback) return OB_INVALID_ARGUMENT;
  output = std::move(declarations);
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { output.reset(); return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { output.reset(); return OB_ERR_UNEXPECTED; }

int ObPluginLoader::execute_function(
    const char *service_id,
    const uint32_t abi_major,
    const uint32_t required_minor,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    const uint32_t argument_count)
{
  if (!impl_) return OB_ALLOCATE_MEMORY_FAILED;
  if (nullptr == service_id || nullptr == context ||
      (argument_count != 0 && nullptr == arguments) ||
      argument_count > SEEKDB_PLUGIN_MAX_ARGUMENTS) {
    return OB_INVALID_ARGUMENT;
  }

  if (!impl_->registry_) return OB_NOT_INIT;
  ObPluginLease lease;
  int ret = impl_->registry_->acquire(service_id, abi_major, required_minor, lease);
  if (OB_SUCCESS != ret) return ret;

  return impl_->execute_lease(lease, context, arguments, argument_count);
}

int ObPluginLoader::execute_extension(
    const seekdb_plugin_extension_kind_t kind,
    const char *sql_name,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    const uint32_t argument_count)
{
  if (!impl_) return OB_ALLOCATE_MEMORY_FAILED;
  if (nullptr == sql_name || nullptr == context ||
      (argument_count != 0 && nullptr == arguments) ||
      argument_count > SEEKDB_PLUGIN_MAX_ARGUMENTS || !impl_->registry_) {
    return OB_INVALID_ARGUMENT;
  }
  std::vector<ObPluginExtensionInfo> candidates;
  uint64_t ignored_epoch = 0;
  int ret = impl_->registry_->find_extensions_by_sql_name(
      kind, sql_name, candidates, ignored_epoch);
  if (OB_SUCCESS != ret) return ret;
  if (candidates.empty()) return OB_ENTRY_NOT_EXIST;

  // SQL names may represent overload sets.  Bind only descriptors whose
  // declared arity accepts this call, then use the stable catalog ordering
  // (priority, cost, object id) to make selection deterministic.
  candidates.erase(
      std::remove_if(candidates.begin(), candidates.end(),
          [argument_count](const ObPluginExtensionInfo &candidate) {
            return argument_count < candidate.spec_.minimum_arity_ ||
                   argument_count > candidate.spec_.maximum_arity_;
          }),
      candidates.end());
  if (candidates.empty()) return OB_INVALID_ARGUMENT;
  std::sort(candidates.begin(), candidates.end(),
      [](const ObPluginExtensionInfo &left,
         const ObPluginExtensionInfo &right) {
        if (left.spec_.priority_ != right.spec_.priority_) {
          return left.spec_.priority_ > right.spec_.priority_;
        }
        if (left.spec_.cost_ != right.spec_.cost_) {
          return left.spec_.cost_ < right.spec_.cost_;
        }
        if (left.spec_.object_id_ != right.spec_.object_id_) {
          return left.spec_.object_id_ < right.spec_.object_id_;
        }
        return left.owner_plugin_id_ < right.owner_plugin_id_;
      });

  ObPluginExtensionLease extension_lease;
  ObPluginLease implementation_lease;
  ret = impl_->registry_->acquire_extension_with_implementation(
      candidates.front(), extension_lease, implementation_lease);
  if (OB_SUCCESS != ret) return ret;
  return impl_->execute_lease(implementation_lease, context, arguments, argument_count);
}

int ObPluginLoader::resolve_sql_extension(
    const seekdb_plugin_extension_kind_t kind,
    const char *sql_name,
    const char *const *argument_type_ids,
    const uint32_t argument_count,
    seekdb_plugin_sql_binding_v1_t &binding) const
try {
  if (!impl_ || !impl_->registry_) return OB_NOT_INIT;
  binding = {};
  ObPluginExtensionInfo extension;
  uint64_t epoch = 0;
  const int ret = impl_->registry_->resolve_sql_extension(
      kind, sql_name, argument_type_ids, argument_count, extension, epoch);
  if (OB_SUCCESS != ret) return ret;

  std::string result_type = extension.spec_.static_result_type_id_;
  if (kind == SEEKDB_PLUGIN_EXTENSION_FUNCTION && result_type.empty()) {
    const int resolved = impl_->resolve_result_type(extension, epoch, argument_type_ids,
        argument_count, result_type);
    if (resolved != OB_SUCCESS) return resolved;
  }
  std::memset(&binding, 0, sizeof(binding));
  binding.struct_size = sizeof(binding);
  binding.kind = extension.spec_.kind_;
  std::memcpy(binding.object_id, extension.spec_.object_id_.data(),
              extension.spec_.object_id_.size());
  std::memcpy(binding.sql_name, extension.spec_.sql_name_.data(),
              extension.spec_.sql_name_.size());
  std::memcpy(binding.result_type_id,
              result_type.data(), result_type.size());
  std::memcpy(binding.owner_plugin_id, extension.owner_plugin_id_.data(),
              extension.owner_plugin_id_.size());
  std::memcpy(binding.physical_format_id,
              extension.spec_.physical_format_id_.data(),
              extension.spec_.physical_format_id_.size());
  binding.owner_generation = extension.owner_generation_;
  binding.catalog_epoch = epoch;
  binding.flags = extension.spec_.flags_;
  binding.minimum_arity = extension.spec_.minimum_arity_;
  binding.maximum_arity = extension.spec_.maximum_arity_;
  binding.column_count =
      static_cast<uint32_t>(extension.spec_.result_columns_.size());
  binding.physical_format_version = extension.spec_.physical_format_version_;
  return OB_SUCCESS;
} catch (const std::bad_alloc &) {
  binding = {}; return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) {
  binding = {}; return OB_ERR_UNEXPECTED;
}

int ObPluginLoader::execute_bound_function(
    const seekdb_plugin_sql_binding_v1_t &binding,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    const uint32_t argument_count)
{
  if (!impl_ || !impl_->registry_) return OB_NOT_INIT;
  if (binding.struct_size < sizeof(binding) ||
      binding.kind != SEEKDB_PLUGIN_EXTENSION_FUNCTION ||
      nullptr == context ||
      (argument_count != 0 && nullptr == arguments) ||
      argument_count < binding.minimum_arity ||
      argument_count > binding.maximum_arity ||
      argument_count > SEEKDB_PLUGIN_MAX_ARGUMENTS) {
    return OB_INVALID_ARGUMENT;
  }

  std::vector<ObPluginExtensionInfo> candidates;
  uint64_t observed_epoch = 0;
  int ret = impl_->registry_->find_extensions_by_sql_name(
      binding.kind, binding.sql_name, candidates, observed_epoch);
  if (OB_SUCCESS != ret) return ret;
  const auto found = std::find_if(
      candidates.begin(), candidates.end(),
      [&binding](const ObPluginExtensionInfo &candidate) {
        return candidate.spec_.object_id_ == binding.object_id &&
               candidate.owner_plugin_id_ == binding.owner_plugin_id &&
               candidate.owner_generation_ == binding.owner_generation;
      });
  if (found == candidates.end()) return OB_ENTRY_NOT_EXIST;

  ObPluginExtensionLease extension_lease;
  ObPluginLease implementation_lease;
  ret = impl_->registry_->acquire_extension_with_implementation(
      *found, extension_lease, implementation_lease);
  if (OB_SUCCESS != ret) return ret;
  return impl_->execute_typed_lease(extension_lease.info()->spec_, binding.catalog_epoch,
      implementation_lease, context, arguments, argument_count);
}

int ObPluginLoader::execute_bound_function_batch(
    const seekdb_plugin_sql_binding_v1_t &binding,
    const seekdb_plugin_batch_context_v1_t *context,
    const seekdb_plugin_batch_row_v1_t *rows, uint32_t row_count)
try {
  if (!impl_ || !impl_->registry_) return OB_NOT_INIT;
  if (binding.struct_size < sizeof(binding) || binding.kind != SEEKDB_PLUGIN_EXTENSION_FUNCTION ||
      !binding.owner_generation || !binding.catalog_epoch || !all_zero(binding.reserved, 4) ||
      !valid_identifier(binding.object_id) || !valid_identifier(binding.sql_name) ||
      !valid_identifier(binding.owner_plugin_id) || !valid_identifier(binding.result_type_id) ||
      !context || context->struct_size < sizeof(*context) || context->reserved_word ||
      !all_zero(context->reserved, 4) || !context->emit_result || !context->query_context ||
      context->query_context->struct_size < sizeof(seekdb_plugin_execution_context_v1_t) ||
      !context->query_context->emit_result || !all_zero(context->query_context->reserved, 6) ||
      row_count > SEEKDB_PLUGIN_MAX_BATCH_ROWS || (row_count && !rows)) return OB_INVALID_ARGUMENT;
  std::vector<ObPluginExtensionInfo> candidates;
  uint64_t epoch = 0;
  int ret = impl_->registry_->find_extensions_by_sql_name(binding.kind, binding.sql_name, candidates, epoch);
  if (ret != OB_SUCCESS) return ret;
  if (epoch != binding.catalog_epoch) return OB_STATE_NOT_MATCH;
  const auto found = std::find_if(candidates.begin(), candidates.end(), [&](const ObPluginExtensionInfo &item) {
    return item.spec_.object_id_ == binding.object_id && item.owner_plugin_id_ == binding.owner_plugin_id &&
        item.owner_generation_ == binding.owner_generation;
  });
  if (found == candidates.end()) return OB_ENTRY_NOT_EXIST;
  ObPluginExtensionLease object;
  ObPluginLease implementation;
  if (OB_SUCCESS != (ret = impl_->registry_->acquire_extension_with_implementation(
      *found, object, implementation, binding.catalog_epoch))) return ret;
  const auto &spec = object.info()->spec_;
  if (spec.flags_ != binding.flags || spec.minimum_arity_ != binding.minimum_arity ||
      spec.maximum_arity_ != binding.maximum_arity ||
      (!spec.static_result_type_id_.empty() && spec.static_result_type_id_ != binding.result_type_id)) return OB_STATE_NOT_MATCH;
  if (OB_SUCCESS != (ret = validate_function_lease(implementation))) return ret;
  auto *instance = impl_->instance_for_lease(implementation);
  if (!instance) return OB_ENTRY_NOT_EXIST;
  const auto *base = static_cast<const seekdb_plugin_function_service_v1_t *>(implementation.service());
  const seekdb_plugin_function_service_v3_t *batch_service = nullptr;
  if (OB_SUCCESS != (ret = get_batch_function_service(base, batch_service))) return ret;
  uint64_t input_bytes = 0;
  for (uint32_t row = 0; row < row_count; ++row) {
    const auto &input = rows[row];
    if (input.struct_size < sizeof(input) || !all_zero(input.reserved, 4) ||
        (row && input.argument_count != rows[0].argument_count) ||
        input.argument_count < spec.minimum_arity_ || input.argument_count > spec.maximum_arity_ ||
        input.argument_count > SEEKDB_PLUGIN_MAX_ARGUMENTS || (input.argument_count && !input.arguments)) return OB_INVALID_ARGUMENT;
    for (uint32_t col = 0; col < input.argument_count; ++col) {
      const auto &value = input.arguments[col];
      if (!valid_execution_argument(value)) return OB_INVALID_ARGUMENT;
      if (!value.is_null) {
        if (value.data_size > SEEKDB_PLUGIN_MAX_BATCH_BYTES - input_bytes) return OB_SIZE_OVERFLOW;
        input_bytes += value.data_size;
      }
    }
  }
  if (!row_count) return OB_SUCCESS;
  if (OB_SUCCESS != (ret = poll_batch_query(context->query_context))) return ret;
  // Pin every cast before executing any conversion. Keep its owned bytes and
  // effective type strings alive through the single batch invocation.
  std::vector<std::vector<ConvertedArgument>> prepared(row_count);
  for (uint32_t row = 0; row < row_count; ++row) {
    if (OB_SUCCESS != (ret = impl_->prepare_arguments(spec, binding.catalog_epoch,
        rows[row].arguments, rows[row].argument_count, prepared[row]))) return ret;
  }
  std::vector<std::vector<seekdb_plugin_execution_value_v1_t>> arguments(row_count);
  std::vector<seekdb_plugin_batch_row_v1_t> effective(row_count);
  uint64_t converted_bytes = 0;
  for (uint32_t row = 0; row < row_count; ++row) {
    if (OB_SUCCESS != (ret = poll_batch_query(context->query_context))) return ret;
    ret = apply_prepared_arguments(prepared[row], context->query_context, rows[row].arguments,
        rows[row].argument_count, [&](const seekdb_plugin_execution_value_v1_t *values, uint32_t count) {
          for (uint32_t col = 0; col < count; ++col) {
            const uint64_t size = values[col].is_null ? 0 : values[col].data_size;
            if (size > SEEKDB_PLUGIN_MAX_BATCH_BYTES - converted_bytes) return OB_SIZE_OVERFLOW;
            converted_bytes += size;
          }
          if (count) arguments[row].assign(values, values + count);
          return OB_SUCCESS;
        });
    if (ret != OB_SUCCESS) return ret;
    effective[row].struct_size = sizeof(effective[row]);
    effective[row].arguments = arguments[row].empty() ? nullptr : arguments[row].data();
    effective[row].argument_count = arguments[row].size();
  }
  BatchResultSink sink{binding.result_type_id, std::vector<BatchResultSink::Row>(row_count)};
  seekdb_plugin_execution_context_v2_t query{};
  if (context->query_context->struct_size >= sizeof(query)) {
    query = *reinterpret_cast<const seekdb_plugin_execution_context_v2_t *>(context->query_context);
    query.v1.struct_size = sizeof(query);
  } else {
    query.v1 = *context->query_context; query.v1.struct_size = sizeof(query.v1);
  }
  query.v1.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  query.v1.emit_result = reject_batch_scalar_result;
  if (OB_SUCCESS != (ret = poll_batch_query(context->query_context))) return ret;
  if (batch_service) {
    seekdb_plugin_batch_context_v1_t call{}; call.struct_size = sizeof(call);
    call.query_context = &query.v1; call.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
    call.emit_result = emit_batch_result;
    ret = from_plugin_status(batch_service->execute_batch(instance, &call, effective.data(), row_count));
  } else {
    for (uint32_t row = 0; ret == OB_SUCCESS && row < row_count; ++row) {
      if (OB_SUCCESS != (ret = poll_batch_query(context->query_context))) break;
      ScalarBatchSink scalar{&sink, row};
      auto scalar_context = query;
      scalar_context.v1.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&scalar);
      scalar_context.v1.emit_result = emit_scalar_batch_result;
      ret = execute_pinned_function(implementation, instance, &scalar_context.v1,
          effective[row].arguments, effective[row].argument_count);
      if (ret == OB_SUCCESS && sink.error_ != SEEKDB_PLUGIN_STATUS_OK) ret = from_plugin_status(sink.error_);
    }
  }
  if (ret == OB_ITER_END) return OB_INVALID_DATA;
  if (ret != OB_SUCCESS) return ret;
  if (sink.error_ != SEEKDB_PLUGIN_STATUS_OK) return from_plugin_status(sink.error_);
  if (std::any_of(sink.rows_.begin(), sink.rows_.end(), [](const BatchResultSink::Row &row) { return !row.emitted_; })) return OB_INVALID_DATA;
  if (OB_SUCCESS != (ret = poll_batch_query(context->query_context))) return ret;
  // A failed plugin never exposes partial output to the caller's result sink.
  // Delivery can itself fail; like every batch consumer, it must then discard.
  for (uint32_t row = 0; row < row_count; ++row) {
    if (OB_SUCCESS != (ret = poll_batch_query(context->query_context))) return ret;
    const auto &owned = sink.rows_[row];
    seekdb_plugin_execution_result_v1_t result{}; result.struct_size = sizeof(result);
    result.type_id = binding.result_type_id; result.is_null = owned.null_;
    result.data = owned.bytes_.empty() ? nullptr : owned.bytes_.data(); result.data_size = owned.bytes_.size();
    ret = from_plugin_status(context->emit_result(context->host, row, &result));
    if (ret != OB_SUCCESS) return ret == OB_ITER_END ? OB_INVALID_DATA : ret;
  }
  return poll_batch_query(context->query_context);
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int ObPluginLoader::decode_type(const ObPluginExtensionInfo &expected,
    const seekdb_plugin_execution_context_v1_t *context, const uint8_t *encoded, uint64_t encoded_size)
{
  if (!impl_) return OB_ALLOCATE_MEMORY_FAILED;
  return impl_->execute_codec(expected, context, encoded, encoded_size, nullptr, true);
}

int ObPluginLoader::encode_type(const ObPluginExtensionInfo &expected,
    const seekdb_plugin_execution_context_v1_t *context, const seekdb_plugin_execution_value_v1_t *value)
{
  if (!impl_) return OB_ALLOCATE_MEMORY_FAILED;
  return impl_->execute_codec(expected, context, nullptr, 0, value, false);
}

int ObPluginLoader::decode_bound_type(const seekdb_plugin_sql_binding_v1_t &binding,
    const seekdb_plugin_execution_context_v1_t *context, const uint8_t *encoded, uint64_t encoded_size)
try {
  if (!impl_) return OB_NOT_INIT;
  ObPluginExtensionInfo expected;
  const int ret = impl_->find_bound_type(binding, expected);
  if (ret != OB_SUCCESS) return ret;
  // execute_codec atomically reacquires the exact object/code identity after
  // lookup. A concurrent disable/reload cannot substitute a new generation.
  return impl_->execute_codec(expected, context, encoded, encoded_size, nullptr, true);
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int ObPluginLoader::encode_bound_type(const seekdb_plugin_sql_binding_v1_t &binding,
    const seekdb_plugin_execution_context_v1_t *context, const seekdb_plugin_execution_value_v1_t *value)
try {
  if (!impl_) return OB_NOT_INIT;
  ObPluginExtensionInfo expected;
  const int ret = impl_->find_bound_type(binding, expected);
  if (ret != OB_SUCCESS) return ret;
  return impl_->execute_codec(expected, context, nullptr, 0, value, false);
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int ObPluginLoader::resolve_type_by_id(const char *logical_type_id,
    seekdb_plugin_sql_binding_v1_t &binding, uint64_t expected_epoch) const
try {
  binding = {};
  if (!impl_ || !impl_->registry_) return OB_NOT_INIT;
  ObPluginExtensionInfo extension;
  uint64_t epoch = 0;
  const int ret = impl_->registry_->find_type_by_id(logical_type_id, extension, epoch, expected_epoch);
  if (ret != OB_SUCCESS) return ret;
  const auto &spec = extension.spec_;
  seekdb_plugin_sql_binding_v1_t candidate{};
  candidate.struct_size = sizeof(candidate);
  candidate.kind = SEEKDB_PLUGIN_EXTENSION_TYPE;
  // The registry validates these bounded identifiers before publication.
  std::memcpy(candidate.object_id, spec.object_id_.data(), spec.object_id_.size());
  std::memcpy(candidate.sql_name, spec.sql_name_.data(), spec.sql_name_.size());
  std::memcpy(candidate.owner_plugin_id, extension.owner_plugin_id_.data(), extension.owner_plugin_id_.size());
  std::memcpy(candidate.physical_format_id, spec.physical_format_id_.data(), spec.physical_format_id_.size());
  candidate.owner_generation = extension.owner_generation_;
  candidate.catalog_epoch = epoch;
  candidate.flags = spec.flags_;
  candidate.physical_format_version = spec.physical_format_version_;
  binding = candidate;
  return OB_SUCCESS;
} catch (const std::bad_alloc &) {
  binding = {}; return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) {
  binding = {}; return OB_ERR_UNEXPECTED;
}

int ObPluginLoader::check_bound_type_comparison(const seekdb_plugin_sql_binding_v1_t &binding)
try {
  if (!impl_) return OB_NOT_INIT;
  int32_t unused = 0;
  return impl_->type_comparison(binding, nullptr, nullptr, unused);
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int ObPluginLoader::compare_bound_type(const seekdb_plugin_sql_binding_v1_t &binding,
    const seekdb_plugin_execution_value_v1_t &left,
    const seekdb_plugin_execution_value_v1_t &right, int32_t &ordering)
{
  ordering = 0;
  try {
    if (!impl_) return OB_NOT_INIT;
    return impl_->type_comparison(binding, &left, &right, ordering);
  } catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) { return OB_ERR_UNEXPECTED; }
}

int ObPluginLoader::resolve_common_type(const char *const *type_ids, uint32_t count,
    std::string &common_type, uint64_t &registry_epoch) const
{
  common_type.clear(); registry_epoch = 0;
  if (!impl_ || !impl_->registry_) return OB_NOT_INIT;
  return impl_->registry_->resolve_common_type(type_ids, count, common_type, registry_epoch);
}

int ObPluginLoader::resolve_sql_cast(const char *source_type_id, const char *target_type_id,
    seekdb_plugin_cast_context_t requested_context, seekdb_plugin_sql_cast_binding_v1_t &binding,
    uint64_t expected_epoch) const
try {
  binding = {};
  if (!impl_ || !impl_->registry_) return OB_NOT_INIT;
  ObPluginExtensionInfo selected;
  uint64_t epoch = 0;
  const int ret = impl_->registry_->resolve_cast(source_type_id, target_type_id, requested_context, selected, epoch);
  if (ret != OB_SUCCESS) return ret;
  if (expected_epoch && epoch != expected_epoch) return OB_STATE_NOT_MATCH;
  seekdb_plugin_sql_cast_binding_v1_t resolved = {};
  resolved.struct_size = sizeof(resolved);
  resolved.requested_context = requested_context;
  resolved.declared_context = selected.spec_.cast_context_;
  std::memcpy(resolved.object_id, selected.spec_.object_id_.data(), selected.spec_.object_id_.size());
  std::memcpy(resolved.owner_plugin_id, selected.owner_plugin_id_.data(), selected.owner_plugin_id_.size());
  std::memcpy(resolved.source_type_id, selected.spec_.source_type_id_.data(), selected.spec_.source_type_id_.size());
  std::memcpy(resolved.target_type_id, selected.spec_.target_type_id_.data(), selected.spec_.target_type_id_.size());
  resolved.owner_generation = selected.owner_generation_;
  resolved.catalog_epoch = epoch;
  binding = resolved;
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int ObPluginLoader::execute_bound_cast(const seekdb_plugin_sql_cast_binding_v1_t &binding,
    const seekdb_plugin_execution_context_v1_t *context, const seekdb_plugin_execution_value_v1_t *value)
try {
  if (binding.struct_size < sizeof(binding) ||
      binding.requested_context < SEEKDB_PLUGIN_CAST_EXPLICIT || binding.requested_context > SEEKDB_PLUGIN_CAST_IMPLICIT ||
      binding.declared_context < binding.requested_context || binding.declared_context > SEEKDB_PLUGIN_CAST_IMPLICIT ||
      !valid_identifier(binding.object_id) || !valid_identifier(binding.owner_plugin_id) ||
      !valid_identifier(binding.source_type_id) || !valid_identifier(binding.target_type_id) ||
      !binding.owner_generation || !binding.catalog_epoch || binding.reserved_word ||
      !all_zero(binding.reserved, sizeof(binding.reserved) / sizeof(binding.reserved[0]))) return OB_INVALID_ARGUMENT;
  ObPluginExtensionInfo expected;
  expected.spec_.kind_ = SEEKDB_PLUGIN_EXTENSION_CAST;
  expected.spec_.object_id_ = binding.object_id;
  expected.spec_.source_type_id_ = binding.source_type_id;
  expected.spec_.target_type_id_ = binding.target_type_id;
  expected.spec_.cast_context_ = binding.declared_context;
  expected.owner_plugin_id_ = binding.owner_plugin_id;
  expected.owner_generation_ = binding.owner_generation;
  return execute_cast(expected, context, value, binding.catalog_epoch);
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int ObPluginLoader::execute_cast(const ObPluginExtensionInfo &expected,
    const seekdb_plugin_execution_context_v1_t *context, const seekdb_plugin_execution_value_v1_t *value,
    const uint64_t expected_epoch)
{
  if (!impl_ || !impl_->registry_) return OB_NOT_INIT;
  if (expected.spec_.kind_ != SEEKDB_PLUGIN_EXTENSION_CAST || !context ||
      context->struct_size < sizeof(*context) || !context->emit_result || !value ||
      value->struct_size < sizeof(*value) || (!value->is_null &&
      (!valid_identifier(value->type_id) || value->data_size > UINT64_C(16777216) ||
       (value->data_size && !value->data)))) return OB_INVALID_ARGUMENT;
  ObPluginExtensionLease object;
  ObPluginLease implementation;
  int ret = impl_->registry_->acquire_extension_with_implementation(expected, object, implementation, expected_epoch);
  if (ret != OB_SUCCESS) return ret;
  const auto &actual = object.info()->spec_;
  if (expected.spec_.source_type_id_ != actual.source_type_id_ ||
      expected.spec_.target_type_id_ != actual.target_type_id_ ||
      expected.spec_.cast_context_ != actual.cast_context_ ||
      (!value->is_null && actual.source_type_id_ != value->type_id)) return OB_INVALID_ARGUMENT;
  return impl_->execute_lease(implementation, context, value, 1);
}

int ObPluginLoader::describe_sql_column(
    const seekdb_plugin_sql_binding_v1_t &binding,
    const uint32_t column_index,
    seekdb_plugin_sql_column_v1_t &column) const
try {
  column = {};
  if (!impl_ || !impl_->registry_) return OB_NOT_INIT;
  if (binding.struct_size < sizeof(binding) ||
      binding.kind != SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION ||
      column_index >= binding.column_count) {
    return OB_INVALID_ARGUMENT;
  }
  ObPluginExtensionInfo found;
  const int ret = impl_->find_bound_table(binding, found);
  if (OB_SUCCESS != ret) return ret;
  if (column_index >= found.spec_.result_columns_.size()) {
    return OB_INVALID_DATA;
  }
  const PluginSqlColumn &source = found.spec_.result_columns_[column_index];
  std::memset(&column, 0, sizeof(column));
  column.struct_size = sizeof(column);
  std::memcpy(column.sql_name, source.sql_name_.data(), source.sql_name_.size());
  std::memcpy(column.type_id, source.type_id_.data(), source.type_id_.size());
  column.nullable = source.nullable_ ? 1 : 0;
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { column = {}; return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { column = {}; return OB_ERR_UNEXPECTED; }

namespace {
bool valid_table_estimate(const seekdb_plugin_table_estimate_v1_t &result)
{
  return result.struct_size == sizeof(result) && result.reserved_word == 0 && all_zero(result.reserved, 4) &&
      std::isfinite(result.rows) && result.rows >= 0 &&
      std::isfinite(result.row_width) && result.row_width >= 0 &&
      std::isfinite(result.total_cost) && result.total_cost >= 0;
}
}

int ObPluginLoader::bind_custom_executor(const char *service_id, uint32_t major, uint32_t minimum_minor,
    CustomExecutorBinding &binding)
try {
  binding = {};
  if (!impl_ || !impl_->registry_) return OB_NOT_INIT;
  if (!valid_identifier(service_id) || !major) return OB_INVALID_ARGUMENT;
  ObPluginLease code;
  int ret = impl_->registry_->acquire(service_id, major, minimum_minor, code);
  if (ret != OB_SUCCESS) return ret;
  if (!(code.service_capabilities() & SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE)) return OB_NOT_SUPPORTED;
  CustomExecutorBinding result;
  if (!impl_->instance_for_lease(code, true, &result.runtime_incarnation) ||
      !valid_custom_service(static_cast<const seekdb_plugin_custom_executor_v1_t *>(code.service()))) return OB_NOT_SUPPORTED;
  result.service_id = service_id; result.owner_id = code.owner_plugin_id();
  result.generation = code.owner_generation(); result.major = major;
  result.minor = code.service_minor(); result.patch = code.service_patch();
  binding = std::move(result);
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { binding = {}; return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { binding = {}; return OB_ERR_UNEXPECTED; }

int ObPluginLoader::open_custom_executor(const CustomExecutorBinding &binding, const uint8_t *plan,
    uint32_t plan_size, std::unique_ptr<ICustomExecutor> &cursor)
try {
  if (!impl_ || !impl_->registry_) return OB_NOT_INIT;
  if (cursor || !valid_identifier(binding.service_id) || !valid_identifier(binding.owner_id) ||
      !valid_identifier(binding.runtime_incarnation) ||
      !binding.generation || !binding.major || plan_size > SEEKDB_PLUGIN_CUSTOM_MAX_PLAN_BYTES ||
      (plan_size && !plan)) return OB_INVALID_ARGUMENT;
  ObPluginLease code;
  int ret = impl_->registry_->acquire(binding.service_id.c_str(), binding.major, binding.minor, binding.patch, 0, code);
  if (ret != OB_SUCCESS) return ret;
  if (!(code.service_capabilities() & SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE)) return OB_NOT_SUPPORTED;
  if (binding.owner_id != code.owner_plugin_id() || binding.generation != code.owner_generation() ||
      binding.minor != code.service_minor() || binding.patch != code.service_patch()) return OB_STATE_NOT_MATCH;
  std::string incarnation;
  auto *instance = impl_->instance_for_lease(code, true, &incarnation);
  if (!instance) return OB_NOT_SUPPORTED;
  if (incarnation != binding.runtime_incarnation) return OB_STATE_NOT_MATCH;
  const auto *service = static_cast<const seekdb_plugin_custom_executor_v1_t *>(code.service());
  if (!valid_custom_service(service)) return OB_NOT_SUPPORTED;
  auto result = std::make_unique<CustomExecutorCursor>(std::move(code), instance, *service);
  ret = result->open(plan, plan_size);
  if (ret == OB_SUCCESS) cursor = std::move(result);
  return ret;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int ObPluginLoader::estimate_bound_table_function(const seekdb_plugin_sql_binding_v1_t &binding,
    seekdb_plugin_table_estimate_v1_t &estimate)
try {
  estimate = {};
  if (!impl_ || !impl_->registry_) return OB_NOT_INIT;
  ObPluginExtensionInfo found;
  int ret = impl_->find_bound_table(binding, found);
  if (ret != OB_SUCCESS) return ret;
  ObPluginExtensionLease object;
  ObPluginLease code;
  ret = impl_->registry_->acquire_extension_with_implementation(found, object, code, binding.catalog_epoch);
  if (ret != OB_SUCCESS) return ret;
  const auto *base = static_cast<const seekdb_plugin_table_function_service_v1_t *>(code.service());
  if (!base || base->struct_size < sizeof(*base) || base->spi_major != SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR ||
      base->reserved_word || !base->open || !base->next || !base->rescan || !base->close ||
      !all_zero(base->reserved, 8)) return OB_NOT_SUPPORTED;
  seekdb_plugin_table_estimate_v1_t result = {};
  result.struct_size = sizeof(result);
  const seekdb_plugin_table_function_service_v2_t *service = nullptr;
  if (base->spi_minor >= SEEKDB_PLUGIN_TABLE_PLANNING_MINOR) {
    if (base->struct_size < sizeof(*service)) return OB_NOT_SUPPORTED;
    service = reinterpret_cast<const seekdb_plugin_table_function_service_v2_t *>(base);
    if (!all_zero(service->reserved, 4) || (!service->estimate && base->spi_minor < SEEKDB_PLUGIN_TABLE_QUERY_CONTROL_MINOR))
      return OB_NOT_SUPPORTED;
  }
  if (!service || !service->estimate) {
    result.rows = 199; result.row_width = 199; result.total_cost = 1;
  } else {
    auto *instance = impl_->instance_for_lease(code);
    if (!instance) return OB_ENTRY_NOT_EXIST;
    const auto &spec = object.info()->spec_;
    std::vector<const char *> types;
    types.reserve(spec.argument_type_ids_.size());
    for (const auto &type : spec.argument_type_ids_) types.push_back(type.c_str());
    seekdb_plugin_table_planning_info_v1_t info = {};
    info.struct_size = sizeof(info); info.object_id = spec.object_id_.c_str();
    info.argument_type_ids = types.empty() ? nullptr : types.data(); info.argument_count = types.size();
    info.column_count = spec.result_columns_.size();
    // A raw callback that claims success without initializing its estimates
    // must not accidentally create a zero-cost, zero-row plan.
    result.rows = result.row_width = result.total_cost = std::numeric_limits<double>::quiet_NaN();
    const auto status = service->estimate(instance, &info, &result);
    if (status == SEEKDB_PLUGIN_STATUS_END_OF_STREAM) return OB_INVALID_DATA;
    if ((ret = from_plugin_status(status)) != OB_SUCCESS) return ret;
    if (!valid_table_estimate(result)) return OB_INVALID_DATA;
  }
  estimate = result;
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { estimate = {}; return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { estimate = {}; return OB_ERR_UNEXPECTED; }

int ObPluginLoader::open_bound_table_function(
    const seekdb_plugin_sql_binding_v1_t &binding,
    const seekdb_plugin_table_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    const uint32_t argument_count,
    std::unique_ptr<IPluginTableCursor> &cursor)
try {
  if (!impl_ || !impl_->registry_) return OB_NOT_INIT;
  if (cursor || binding.struct_size < sizeof(binding) ||
      binding.kind != SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION ||
      nullptr == context || context->struct_size < sizeof(*context) ||
      nullptr == context->emit_row ||
      (argument_count != 0 && nullptr == arguments) ||
      argument_count < binding.minimum_arity ||
      argument_count > binding.maximum_arity ||
      argument_count > SEEKDB_PLUGIN_MAX_ARGUMENTS) {
    return OB_INVALID_ARGUMENT;
  }

  ObPluginExtensionInfo found;
  int ret = impl_->find_bound_table(binding, found);
  if (OB_SUCCESS != ret) return ret;

  ObPluginExtensionLease extension_lease;
  ObPluginLease implementation_lease;
  ret = impl_->registry_->acquire_extension_with_implementation(
      found, extension_lease, implementation_lease, binding.catalog_epoch);
  if (OB_SUCCESS != ret) return ret;
  if (!implementation_lease.is_valid() ||
      implementation_lease.service_minor() < SEEKDB_PLUGIN_EXECUTION_SPI_MINOR) {
    return OB_STATE_NOT_MATCH;
  }
  const auto *service =
      reinterpret_cast<const seekdb_plugin_table_function_service_v1_t *>(
          implementation_lease.service());
  if (nullptr == service || service->struct_size < sizeof(*service) ||
      service->spi_major != SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR ||
      service->spi_minor < SEEKDB_PLUGIN_EXECUTION_SPI_MINOR ||
      service->reserved_word != 0 || nullptr == service->open ||
      nullptr == service->next || nullptr == service->rescan ||
      nullptr == service->close ||
      !all_zero(service->reserved,
                sizeof(service->reserved) / sizeof(service->reserved[0]))) {
    return OB_NOT_SUPPORTED;
  }

  auto *instance = impl_->instance_for_lease(implementation_lease);
  if (nullptr == instance) return OB_ENTRY_NOT_EXIST;

  std::vector<ConvertedArgument> prepared;
  if (OB_SUCCESS != (ret = impl_->prepare_arguments(extension_lease.info()->spec_, binding.catalog_epoch,
      arguments, argument_count, prepared))) return ret;
  if (impl_->registry_->registry_epoch() != binding.catalog_epoch) return OB_STATE_NOT_MATCH;
  seekdb_plugin_execution_context_v1_t cast_context = {}; cast_context.struct_size = sizeof(cast_context);
  seekdb_plugin_table_cursor_handle_t *plugin_cursor = nullptr;
  ret = apply_prepared_arguments(prepared, &cast_context, arguments, argument_count,
      [&](const seekdb_plugin_execution_value_v1_t *inputs, uint32_t count) {
        // Strictness is about the declared signature AFTER coercion. Casts
        // themselves need not propagate NULL, and still run under their leases.
        if (null_propagating_table_input(binding.flags, inputs, count)) return OB_ITER_END;
        auto legacy = *context;
        seekdb_plugin_table_execution_context_v2_t control{};
        seekdb_plugin_table_execution_context_v3_t sql{};
        const auto *selected = context;
        if (service->spi_minor < SEEKDB_PLUGIN_TABLE_QUERY_CONTROL_MINOR && context->struct_size > sizeof(*context)) {
          legacy.struct_size = sizeof(legacy); selected = &legacy;
        } else if (service->spi_minor == SEEKDB_PLUGIN_TABLE_QUERY_CONTROL_MINOR && context->struct_size > sizeof(control)) {
          control = *reinterpret_cast<const seekdb_plugin_table_execution_context_v2_t *>(context);
          control.v1.struct_size = sizeof(control); selected = &control.v1;
        } else if (service->spi_minor == SEEKDB_PLUGIN_TABLE_SQL_CONTEXT_MINOR && context->struct_size > sizeof(sql)) {
          sql = *reinterpret_cast<const seekdb_plugin_table_execution_context_v3_t *>(context);
          sql.v2.v1.struct_size = sizeof(sql); selected = &sql.v2.v1;
        }
        return from_plugin_status(service->open(instance, selected, inputs, count, &plugin_cursor));
      });
  if (OB_SUCCESS != ret) {
    if (plugin_cursor) { try { static_cast<void>(service->close(instance, plugin_cursor)); } catch (...) {} }
    return ret;
  }
  if (nullptr == plugin_cursor) return OB_INVALID_DATA;

  PluginTableCursor *owner = new (std::nothrow) PluginTableCursor(
      std::move(extension_lease), std::move(implementation_lease),
      instance, service, plugin_cursor, std::move(prepared));
  if (nullptr == owner) {
    try {
      static_cast<void>(service->close(instance, plugin_cursor));
    } catch (...) {
    }
    return OB_ALLOCATE_MEMORY_FAILED;
  }
  cursor.reset(owner);
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int ObPluginLoader::run_optimizer_hooks(const seekdb_plugin_optimizer_info_v1_t &info,
    int (*next)(void *), void *context)
try {
  if (!impl_ || !impl_->registry_) return OB_NOT_INIT;
  if (!next || info.struct_size < sizeof(info) || info.statement_kind > SEEKDB_PLUGIN_OPTIMIZER_EXPLAIN ||
      !all_zero(info.reserved, 4)) return OB_INVALID_ARGUMENT;
  static thread_local uint32_t depth = 0;
  if (depth >= 16) return OB_SIZE_OVERFLOW;
  struct Depth { uint32_t &value; explicit Depth(uint32_t &v) : value(v) { ++value; } ~Depth() { --value; } } guard(depth);
  std::vector<ObPluginExtensionInfo> hooks;
  uint64_t epoch = 0;
  int ret = impl_->registry_->find_hooks(SEEKDB_PLUGIN_EXTENSION_OPTIMIZER_HOOK,
      SEEKDB_PLUGIN_OPTIMIZER_HOOK_POINT, hooks, epoch);
  if (ret != OB_SUCCESS) return ret;
  if (hooks.size() > 64) return OB_SIZE_OVERFLOW;
  struct Invocation {
    ObPluginExtensionLease object;
    ObPluginLease code;
    seekdb_plugin_instance_handle_t *instance = nullptr;
    const seekdb_plugin_optimizer_service_v1_t *service = nullptr;
    const seekdb_plugin_optimizer_info_v1_t *info = nullptr;
    static int32_t invoke(void *opaque, seekdb_runtime_hook_next_fn next, void *frame) noexcept {
      auto &self = *static_cast<Invocation *>(opaque);
      struct Continuation {
        seekdb_runtime_hook_next_fn next;
        void *frame;
        bool invalid = false;
        static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL call(void *opaque, int32_t *error) noexcept {
          auto &self = *static_cast<Continuation *>(opaque);
          if (!error || self.invalid) { self.invalid = true; return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT; }
          const int ret = self.next(self.frame);
          *error = ret;
          return to_plugin_status(ret);
        }
      } continuation{next, frame};
      seekdb_plugin_optimizer_context_v1_t context = {};
      context.struct_size = sizeof(context); context.info = self.info;
      context.continuation = &continuation; context.next = Continuation::call;
      try {
        const auto status = self.service->invoke(self.instance, &context);
        if (continuation.invalid || status == SEEKDB_PLUGIN_STATUS_END_OF_STREAM) return OB_INVALID_DATA;
        return from_plugin_status(status);
      } catch (...) { return OB_ERR_UNEXPECTED; }
    }
  };
  std::vector<Invocation> pinned(hooks.size());
  std::vector<seekdb_runtime_hook_v2_t> chain(hooks.size());
  // Pin and validate the ENTIRE ordered chain before entering any callback.
  for (size_t i = 0; i < hooks.size(); ++i) {
    auto &entry = pinned[i];
    ret = impl_->registry_->acquire_extension_with_implementation(hooks[i], entry.object, entry.code, epoch);
    if (ret != OB_SUCCESS) return ret;
    entry.service = static_cast<const seekdb_plugin_optimizer_service_v1_t *>(entry.code.service());
    const auto *service = entry.service;
    if (!service || service->struct_size < sizeof(*service) || service->spi_major != 1 || service->spi_minor != 0 ||
        service->reserved_word || !service->invoke || !all_zero(service->reserved, 4)) return OB_NOT_SUPPORTED;
    entry.instance = impl_->instance_for_lease(entry.code);
    if (!entry.instance) return OB_ENTRY_NOT_EXIST;
    entry.info = &info;
    // Public optimizer v1 remains an around hook. Mode-aware runtime support
    // does not by itself grant private plan access or replacement permission.
    chain[i] = {sizeof(seekdb_runtime_hook_v2_t), SEEKDB_RUNTIME_HOOK_AROUND,
                &entry, Invocation::invoke, nullptr, {0, 0, 0, 0}};
  }
  if (impl_->registry_->registry_epoch() != epoch) return OB_STATE_NOT_MATCH;
  struct Leaf {
    int (*next)(void *); void *context;
    bool called = false;
    static int32_t invoke(void *opaque) noexcept {
      auto &self = *static_cast<Leaf *>(opaque);
      self.called = true;
      try { return self.next(self.context); } catch (...) { return OB_ERR_UNEXPECTED; }
    }
    static int32_t validate(void *opaque) noexcept {
      // This adapter only admits around hooks; a successful chain must have
      // reached the core planner. Future replacement adapters need their own
      // result/type/ownership validation, not this around-only invariant.
      return static_cast<Leaf *>(opaque)->called ? OB_SUCCESS : OB_STATE_NOT_MATCH;
    }
  } leaf{next, context};
  int32_t result = OB_ERR_UNEXPECTED;
  const int32_t status = seekdb_runtime_hook_run_v2(chain.data(), chain.size(),
      Leaf::invoke, &leaf, Leaf::validate, OB_STATE_NOT_MATCH, &result);
  return status == SEEKDB_RUNTIME_OK ? result : OB_STATE_NOT_MATCH;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int ObPluginLoader::plugin_join_hooks_available(bool &available)
{
  return candidate_hooks_available(SEEKDB_PLUGIN_PHASE_JOIN, available);
}

int ObPluginLoader::candidate_hooks_available(seekdb_plugin_candidate_phase_t phase, bool &available)
try {
  available = false;
  const char *point = seekdb_plugin_candidate_hook_point(phase);
  if (!point) return OB_INVALID_ARGUMENT;
  if (!impl_ || !impl_->registry_) return OB_NOT_INIT;
  std::vector<ObPluginExtensionInfo> hooks;
  uint64_t epoch = 0;
  const int ret = impl_->registry_->find_hooks(SEEKDB_PLUGIN_EXTENSION_OPTIMIZER_HOOK,
      point, hooks, epoch);
  if (ret == OB_SUCCESS) available = !hooks.empty();
  return ret;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int ObPluginLoader::run_candidate_hooks(const seekdb_plugin_candidate_context_v1_t &view,
    int (*next)(void *), void *context, int (*validate)(void *), seekdb_plugin_candidate_phase_t phase)
try {
  if (!impl_ || !impl_->registry_) return OB_NOT_INIT;
  const char *point = seekdb_plugin_candidate_hook_point(phase);
  if (!point) return OB_INVALID_ARGUMENT;
  const bool contributing = phase != SEEKDB_PLUGIN_PHASE_SELECT;
  const auto *values = view.struct_size == sizeof(seekdb_plugin_candidate_context_v8_t) ?
      reinterpret_cast<const seekdb_plugin_candidate_context_v8_t *>(&view) : nullptr;
  const auto *sorts = values ? &values->v7 : view.struct_size == sizeof(seekdb_plugin_candidate_context_v7_t) ?
      reinterpret_cast<const seekdb_plugin_candidate_context_v7_t *>(&view) : nullptr;
  const auto *bindings = sorts ? &sorts->v6 : view.struct_size == sizeof(seekdb_plugin_candidate_context_v6_t) ?
      reinterpret_cast<const seekdb_plugin_candidate_context_v6_t *>(&view) : nullptr;
  const auto *semantics = bindings ? &bindings->v5 : view.struct_size == sizeof(seekdb_plugin_candidate_context_v5_t) ?
      reinterpret_cast<const seekdb_plugin_candidate_context_v5_t *>(&view) : nullptr;
  const auto *query = semantics ? &semantics->v4 : view.struct_size == sizeof(seekdb_plugin_candidate_context_v4_t) ?
      reinterpret_cast<const seekdb_plugin_candidate_context_v4_t *>(&view) : nullptr;
  const auto *graph = query ? &query->v3 : view.struct_size == sizeof(seekdb_plugin_candidate_context_v3_t) ?
      reinterpret_cast<const seekdb_plugin_candidate_context_v3_t *>(&view) : nullptr;
  const auto *builders = graph ? &graph->v2 : view.struct_size == sizeof(seekdb_plugin_candidate_context_v2_t) ?
      reinterpret_cast<const seekdb_plugin_candidate_context_v2_t *>(&view) : nullptr;
  if (!next || !validate || (!builders && view.struct_size != sizeof(view)) || !view.candidate_count ||
      !view.host_context || !view.get || !view.select || view.next || view.continuation ||
      !all_zero(view.reserved, 4)) return OB_INVALID_ARGUMENT;
  if (builders && (!builders->current_count || !builders->build || !builders->get_error ||
      !all_zero(builders->reserved, 4))) return OB_INVALID_ARGUMENT;
  if (graph && (!graph->root || !graph->plan || !graph->child || !graph->expression ||
      !graph->describe_expression || !graph->argument || !all_zero(graph->reserved, 4))) return OB_INVALID_ARGUMENT;
  if (query && (!query->query || !query->target || !all_zero(query->reserved, 4))) return OB_INVALID_ARGUMENT;
  if (semantics && (!semantics->plan_semantics || !semantics->expression_semantics || !semantics->scope ||
      !semantics->column_count || !semantics->column || !all_zero(semantics->reserved, 4))) return OB_INVALID_ARGUMENT;
  if (bindings && (!bindings->binding_count || !bindings->binding || !all_zero(bindings->reserved, 4))) return OB_INVALID_ARGUMENT;
  if (sorts && (!sorts->sort_info || !sorts->sort_key || !all_zero(sorts->reserved, 4))) return OB_INVALID_ARGUMENT;
  if (values && (!values->value_info || !all_zero(values->reserved, 4))) return OB_INVALID_ARGUMENT;
  if (contributing && !builders) return OB_INVALID_ARGUMENT;
  static thread_local uint32_t depth = 0;
  if (depth >= 16) return OB_SIZE_OVERFLOW;
  struct Depth { uint32_t &n; explicit Depth(uint32_t &v) : n(v) { ++n; } ~Depth() { --n; } } guard(depth);
  std::vector<ObPluginExtensionInfo> hooks;
  uint64_t epoch = 0;
  int ret = impl_->registry_->find_hooks(SEEKDB_PLUGIN_EXTENSION_OPTIMIZER_HOOK,
      point, hooks, epoch);
  if (ret != OB_SUCCESS) return ret;
  if (hooks.size() > 64) return OB_SIZE_OVERFLOW;
  struct Invocation {
    ObPluginExtensionLease object;
    ObPluginLease code;
    seekdb_plugin_instance_handle_t *instance = nullptr;
    const seekdb_plugin_candidate_service_v1_t *service = nullptr;
    const seekdb_plugin_candidate_context_v1_t *view = nullptr;
    const seekdb_plugin_candidate_context_v2_t *builders = nullptr;
    const seekdb_plugin_candidate_context_v3_t *graph = nullptr;
    const seekdb_plugin_candidate_context_v4_t *query = nullptr;
    const seekdb_plugin_candidate_context_v5_t *semantics = nullptr;
    const seekdb_plugin_candidate_context_v6_t *bindings = nullptr;
    const seekdb_plugin_candidate_context_v7_t *sorts = nullptr;
    const seekdb_plugin_candidate_context_v8_t *values = nullptr;
    static int32_t invoke(void *opaque, seekdb_runtime_hook_next_fn next, void *frame) noexcept {
      auto &self = *static_cast<Invocation *>(opaque);
      struct Continuation {
        seekdb_runtime_hook_next_fn next; void *frame; bool invalid = false;
        static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL call(void *opaque, int32_t *error) noexcept {
          auto &self = *static_cast<Continuation *>(opaque);
          if (!error || self.invalid) { self.invalid = true; return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT; }
          *error = self.next(self.frame);
          return to_plugin_status(*error);
        }
      } continuation{next, frame};
      auto view = *self.view;
      view.struct_size = sizeof(view);
      if (self.builders) view.candidate_count = self.builders->current_count(view.host_context);
      view.next = Continuation::call;
      view.continuation = &continuation;
      try {
        seekdb_plugin_status_t status;
        if (self.service->spi_minor == 7) {
          auto extended = *self.values;
          extended.v7.v6.v5.v4.v3.v2.v1 = view;
          extended.v7.v6.v5.v4.v3.v2.v1.struct_size = sizeof(extended);
          status = self.service->invoke(self.instance, &extended.v7.v6.v5.v4.v3.v2.v1);
        } else if (self.service->spi_minor == 6) {
          auto extended = *self.sorts;
          extended.v6.v5.v4.v3.v2.v1 = view;
          extended.v6.v5.v4.v3.v2.v1.struct_size = sizeof(extended);
          status = self.service->invoke(self.instance, &extended.v6.v5.v4.v3.v2.v1);
        } else if (self.service->spi_minor == 5) {
          auto extended = *self.bindings;
          extended.v5.v4.v3.v2.v1 = view;
          extended.v5.v4.v3.v2.v1.struct_size = sizeof(extended);
          status = self.service->invoke(self.instance, &extended.v5.v4.v3.v2.v1);
        } else if (self.service->spi_minor == 4) {
          auto extended = *self.semantics;
          extended.v4.v3.v2.v1 = view;
          extended.v4.v3.v2.v1.struct_size = sizeof(extended);
          status = self.service->invoke(self.instance, &extended.v4.v3.v2.v1);
        } else if (self.service->spi_minor == 3) {
          auto extended = *self.query;
          extended.v3.v2.v1 = view;
          extended.v3.v2.v1.struct_size = sizeof(extended);
          status = self.service->invoke(self.instance, &extended.v3.v2.v1);
        } else if (self.service->spi_minor == 2) {
          auto extended = *self.graph;
          extended.v2.v1 = view;
          extended.v2.v1.struct_size = sizeof(extended);
          status = self.service->invoke(self.instance, &extended.v2.v1);
        } else if (self.service->spi_minor == 1) {
          auto extended = *self.builders;
          extended.v1 = view;
          extended.v1.struct_size = sizeof(extended);
          status = self.service->invoke(self.instance, &extended.v1);
        } else {
          status = self.service->invoke(self.instance, &view);
        }
        if (continuation.invalid || status == SEEKDB_PLUGIN_STATUS_END_OF_STREAM) return OB_INVALID_DATA;
        if (self.builders) {
          const int error = self.builders->get_error(view.host_context);
          if (error != OB_SUCCESS) return error;
        }
        return from_plugin_status(status);
      } catch (...) { return OB_ERR_UNEXPECTED; }
    }
  };
  std::vector<Invocation> pinned(hooks.size());
  std::vector<seekdb_runtime_hook_v2_t> chain(hooks.size());
  for (size_t i = 0; i < hooks.size(); ++i) {
    auto &entry = pinned[i];
    ret = impl_->registry_->acquire_extension_with_implementation(hooks[i], entry.object, entry.code, epoch);
    if (ret != OB_SUCCESS) return ret;
    // A Public implementation must not opt into deep hooks merely by naming
    // this hook point. The code owner passed linked-host admission at load.
    entry.instance = impl_->instance_for_lease(entry.code, true);
    if (!entry.instance) return OB_NOT_SUPPORTED;
    entry.service = static_cast<const seekdb_plugin_candidate_service_v1_t *>(entry.code.service());
    const auto *service = entry.service;
    if (!service || service->struct_size != sizeof(*service) || service->spi_major != 1 ||
        (contributing && (service->spi_minor < 1 || service->mode != SEEKDB_PLUGIN_CANDIDATE_AROUND)) ||
        service->spi_minor > 7 || (service->spi_minor >= 1 && !builders) ||
        (service->spi_minor >= 2 && !graph) || (service->spi_minor >= 3 && !query) ||
        (service->spi_minor >= 4 && !semantics) || (service->spi_minor >= 5 && !bindings) ||
        (service->spi_minor >= 6 && !sorts) || (service->spi_minor == 7 && !values) ||
        !service->invoke || !all_zero(service->reserved, 4) ||
        (service->mode != SEEKDB_PLUGIN_CANDIDATE_AROUND &&
         service->mode != SEEKDB_PLUGIN_CANDIDATE_REPLACE)) return OB_NOT_SUPPORTED;
    entry.view = &view;
    entry.builders = builders;
    entry.graph = graph;
    entry.query = query;
    entry.semantics = semantics;
    entry.bindings = bindings;
    entry.sorts = sorts;
    entry.values = values;
    chain[i] = {sizeof(seekdb_runtime_hook_v2_t), service->mode, &entry,
                Invocation::invoke, nullptr, {0, 0, 0, 0}};
  }
  if (impl_->registry_->registry_epoch() != epoch) return OB_STATE_NOT_MATCH;
  struct Leaf {
    int (*next)(void *); void *context; int (*validate)(void *);
    static int32_t invoke(void *opaque) noexcept {
      auto &self = *static_cast<Leaf *>(opaque);
      try { return self.next(self.context); } catch (...) { return OB_ERR_UNEXPECTED; }
    }
    static int32_t check(void *opaque) noexcept {
      auto &self = *static_cast<Leaf *>(opaque);
      try { return self.validate(self.context); } catch (...) { return OB_ERR_UNEXPECTED; }
    }
  } leaf{next, context, validate};
  int32_t result = OB_ERR_UNEXPECTED;
  const int32_t status = seekdb_runtime_hook_run_v2(chain.data(), chain.size(),
      Leaf::invoke, &leaf, Leaf::check, OB_STATE_NOT_MATCH, &result);
  return status == SEEKDB_RUNTIME_OK ? result : OB_STATE_NOT_MATCH;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int ObPluginLoader::get_status(const std::string &plugin_id,
                               ObPluginStatusSnapshot &status) const
{
  if (!impl_) return OB_ALLOCATE_MEMORY_FAILED;
  std::lock_guard<std::mutex> guard(impl_->mutex_);
  int ret = OB_ENTRY_NOT_EXIST;
  try {
    for (auto it = impl_->modules_.rbegin(); it != impl_->modules_.rend(); ++it) {
      if ((*it)->plugin_id_ == plugin_id) {
        ObPluginStatusSnapshot candidate;
        Impl::fill_status(**it, candidate);
        status = std::move(candidate);
        ret = OB_SUCCESS;
        break;
      }
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObPluginLoader::list_status(std::vector<ObPluginStatusSnapshot> &statuses) const
{
  if (!impl_) return OB_ALLOCATE_MEMORY_FAILED;
  std::lock_guard<std::mutex> guard(impl_->mutex_);
  statuses.clear();
  try {
    statuses.reserve(impl_->modules_.size());
    for (const std::unique_ptr<Impl::Module> &module : impl_->modules_) {
      ObPluginStatusSnapshot status;
      Impl::fill_status(*module, status);
      statuses.push_back(status);
    }
  } catch (const std::bad_alloc &) {
    statuses.clear();
    return OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    statuses.clear();
    return OB_ERR_UNEXPECTED;
  }
  return OB_SUCCESS;
}

std::string ObPluginLoader::last_error() const
{
  if (!impl_) return "plugin loader allocation failed";
  std::lock_guard<std::mutex> guard(impl_->mutex_);
  return impl_->last_error_;
}

ObPluginLoadFailureReason ObPluginLoader::last_failure_reason() const
{
  if (!impl_) return ObPluginLoadFailureReason::OTHER;
  std::lock_guard<std::mutex> guard(impl_->mutex_);
  return impl_->last_failure_reason_;
}

std::string ObPluginLoader::trusted_directory() const
{
  if (!impl_) return std::string();
  std::lock_guard<std::mutex> guard(impl_->mutex_);
  return impl_->trusted_directory_;
}

} // namespace plugin
} // namespace share
} // namespace oceanbase
