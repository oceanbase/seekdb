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

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <set>
#include <utility>

#include "lib/ob_errno.h"

#if defined(_WIN32)
#include <malloc.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
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
const char PLUGIN_ENTRY_SYMBOL[] = "seekdb_plugin_entry_v1";
const seekdb_plugin_capability_t KNOWN_CAPABILITIES =
    SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE |
    SEEKDB_PLUGIN_CAPABILITY_MULTI_INSTANCE |
    SEEKDB_PLUGIN_CAPABILITY_SIDE_BY_SIDE_UPGRADE |
    SEEKDB_PLUGIN_CAPABILITY_PERSISTENT_DATA |
    SEEKDB_PLUGIN_CAPABILITY_TRANSACTIONAL_SERVICES;

struct StagedService
{
  ObPluginServiceSpec spec_;
};

struct HostContext;

struct HostLease
{
  explicit HostLease(HostContext *host) : host_(host), lease_() {}
  HostContext *host_;
  ObPluginLease lease_;
};

struct HostRegistration
{
  explicit HostRegistration(HostContext *host) : host_(host), services_() {}
  HostContext *host_;
  std::vector<StagedService> services_;
};

struct HostContext
{
  HostContext()
      : registry_(nullptr), owner_(), api_(), mutex_(), leases_(), registrations_(), staged_(),
        accepting_registrations_(false)
  {}

  ObPluginServiceRegistry *registry_;
  std::shared_ptr<ObPluginGeneration> owner_;
  seekdb_plugin_host_api_v1_t api_;
  std::mutex mutex_;
  std::set<HostLease *> leases_;
  std::set<HostRegistration *> registrations_;
  std::vector<StagedService> staged_;
  bool accepting_registrations_;
};

bool is_power_of_two(const uint32_t value)
{
  return value != 0 && (value & (value - 1)) == 0;
}

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
  return compare_version(version, range.minimum_inclusive) >= 0 &&
      (unbounded(range.maximum_exclusive) ||
       compare_version(version, range.maximum_exclusive) < 0);
}

bool valid_range(const seekdb_plugin_version_range_t &range)
{
  const size_t required_size = sizeof(seekdb_plugin_version_range_t);
  return range.struct_size >= required_size &&
      (unbounded(range.maximum_exclusive) ||
       compare_version(range.minimum_inclusive, range.maximum_exclusive) < 0) &&
      all_zero(range.reserved, sizeof(range.reserved) / sizeof(range.reserved[0]));
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
    case OB_INVALID_DATA: status = SEEKDB_PLUGIN_STATUS_INVALID_MANIFEST; break;
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
    default: ret = OB_ERR_UNEXPECTED; break;
  }
  return ret;
}

HostContext *as_host(seekdb_plugin_host_handle_t *opaque)
{
  return reinterpret_cast<HostContext *>(opaque);
}

void *SEEKDB_PLUGIN_CALL host_alloc(seekdb_plugin_host_handle_t *,
                                    const uint64_t size,
                                    const uint32_t alignment)
{
  void *memory = nullptr;
  try {
    if (size == 0 || size > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
        !is_power_of_two(alignment)) {
      return nullptr;
    }
#if defined(_WIN32)
    const uint32_t effective_alignment =
        alignment < sizeof(void *) ? static_cast<uint32_t>(sizeof(void *)) : alignment;
    memory = _aligned_malloc(static_cast<size_t>(size), effective_alignment);
#else
    if (alignment <= alignof(std::max_align_t)) {
      memory = std::malloc(static_cast<size_t>(size));
    } else if (alignment >= sizeof(void *) &&
               0 != posix_memalign(&memory, alignment, static_cast<size_t>(size))) {
      memory = nullptr;
    }
#endif
  } catch (...) {
    memory = nullptr;
  }
  return memory;
}

void SEEKDB_PLUGIN_CALL host_free(seekdb_plugin_host_handle_t *,
                                  void *memory,
                                  uint64_t,
                                  uint32_t)
{
  try {
#if defined(_WIN32)
    _aligned_free(memory);
#else
    std::free(memory);
#endif
  } catch (...) {
  }
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
  if (service.struct_size < required_size || !valid_identifier(service.service_id) ||
      service.version.major == 0 || nullptr == service.service ||
      (service.capabilities & ~KNOWN_CAPABILITIES) != 0 ||
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

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL host_begin_registration(
    seekdb_plugin_host_handle_t *opaque,
    seekdb_plugin_registration_txn_t **out_txn)
{
  int ret = OB_SUCCESS;
  try {
    HostContext *host = as_host(opaque);
    if (nullptr != out_txn) *out_txn = nullptr;
    if (nullptr == host || nullptr == out_txn) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      std::lock_guard<std::mutex> guard(host->mutex_);
      if (!host->accepting_registrations_) {
        ret = OB_STATE_NOT_MATCH;
      } else {
        std::unique_ptr<HostRegistration> txn(new (std::nothrow) HostRegistration(host));
        if (!txn) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          host->registrations_.insert(txn.get());
          *out_txn = reinterpret_cast<seekdb_plugin_registration_txn_t *>(txn.release());
        }
      }
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  return to_plugin_status(ret);
}

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL host_register_service(
    seekdb_plugin_host_handle_t *opaque,
    seekdb_plugin_registration_txn_t *opaque_txn,
    const seekdb_plugin_service_provide_descriptor_t *service)
{
  int ret = OB_SUCCESS;
  try {
    HostContext *host = as_host(opaque);
    HostRegistration *txn = reinterpret_cast<HostRegistration *>(opaque_txn);
    if (nullptr == host || nullptr == txn || nullptr == service) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      std::lock_guard<std::mutex> guard(host->mutex_);
      if (!host->accepting_registrations_ || host->registrations_.count(txn) == 0 ||
          txn->host_ != host) {
        ret = OB_STATE_NOT_MATCH;
      } else {
        StagedService staged;
        std::string ignored;
        ret = validate_registration_service(*service, staged, ignored);
        for (const StagedService &item : txn->services_) {
          if (OB_SUCCESS == ret && item.spec_.name_ == staged.spec_.name_ &&
              item.spec_.abi_major_ == staged.spec_.abi_major_) {
            ret = OB_ENTRY_EXIST;
          }
        }
        for (const StagedService &item : host->staged_) {
          if (OB_SUCCESS == ret && item.spec_.name_ == staged.spec_.name_ &&
              item.spec_.abi_major_ == staged.spec_.abi_major_) {
            ret = OB_ENTRY_EXIST;
          }
        }
        if (OB_SUCCESS == ret) txn->services_.push_back(staged);
      }
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  return to_plugin_status(ret);
}

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL host_commit_registration(
    seekdb_plugin_host_handle_t *opaque,
    seekdb_plugin_registration_txn_t *opaque_txn)
{
  int ret = OB_SUCCESS;
  try {
    HostContext *host = as_host(opaque);
    HostRegistration *txn = reinterpret_cast<HostRegistration *>(opaque_txn);
    if (nullptr == host || nullptr == txn) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      std::lock_guard<std::mutex> guard(host->mutex_);
      const auto it = host->registrations_.find(txn);
      if (!host->accepting_registrations_ || it == host->registrations_.end() ||
          txn->host_ != host) {
        ret = OB_STATE_NOT_MATCH;
      } else {
        // Build an isolated candidate so allocation failure leaves both the
        // transaction and the shared staged set completely unchanged.
        std::vector<StagedService> candidate(host->staged_);
        candidate.insert(candidate.end(), txn->services_.begin(), txn->services_.end());
        host->staged_.swap(candidate);
        host->registrations_.erase(it);
        delete txn;
      }
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  return to_plugin_status(ret);
}

void SEEKDB_PLUGIN_CALL host_abort_registration(
    seekdb_plugin_host_handle_t *opaque,
    seekdb_plugin_registration_txn_t *opaque_txn)
{
  try {
    HostContext *host = as_host(opaque);
    HostRegistration *txn = reinterpret_cast<HostRegistration *>(opaque_txn);
    if (nullptr != host && nullptr != txn) {
      std::lock_guard<std::mutex> guard(host->mutex_);
      const auto it = host->registrations_.find(txn);
      if (it != host->registrations_.end() && txn->host_ == host) {
        host->registrations_.erase(it);
        delete txn;
      }
    }
  } catch (...) {
  }
}

void init_host_api(HostContext &host)
{
  std::memset(&host.api_, 0, sizeof(host.api_));
  host.api_.struct_size = sizeof(host.api_);
  host.api_.abi_major = SEEKDB_PLUGIN_ABI_MAJOR;
  host.api_.abi_minor = SEEKDB_PLUGIN_ABI_MINOR;
  host.api_.host_handle = reinterpret_cast<seekdb_plugin_host_handle_t *>(&host);
  host.api_.alloc = host_alloc;
  host.api_.free = host_free;
  host.api_.log = host_log;
  host.api_.acquire_service = host_acquire_service;
  host.api_.release_service = host_release_service;
  host.api_.begin_registration = host_begin_registration;
  host.api_.register_service = host_register_service;
  host.api_.commit_registration = host_commit_registration;
  host.api_.abort_registration = host_abort_registration;
}

void cleanup_host_resources(HostContext &host)
{
  std::lock_guard<std::mutex> guard(host.mutex_);
  host.accepting_registrations_ = false;
  for (HostRegistration *registration : host.registrations_) delete registration;
  host.registrations_.clear();
  for (HostLease *lease : host.leases_) delete lease;
  host.leases_.clear();
  host.staged_.clear();
}

#if defined(_WIN32)
typedef HMODULE ModuleHandle;
const ModuleHandle INVALID_MODULE = nullptr;

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

ModuleHandle open_module(const std::string &path, std::string &error)
{
  const DWORD flags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
  ModuleHandle module = LoadLibraryExA(path.c_str(), nullptr, flags);
  if (nullptr == module) error = windows_error(GetLastError());
  return module;
}

void close_module(ModuleHandle module)
{
  if (nullptr != module) FreeLibrary(module);
}

int find_entry(ModuleHandle module, seekdb_plugin_entry_v1_fn &entry, std::string &error)
{
  FARPROC symbol = GetProcAddress(module, PLUGIN_ENTRY_SYMBOL);
  if (nullptr == symbol) error = windows_error(GetLastError());
  if (nullptr == symbol) return OB_ENTRY_NOT_EXIST;
  if (sizeof(symbol) != sizeof(entry)) {
    error = "platform function pointer representation is unsupported";
    return OB_NOT_SUPPORTED;
  }
  std::memcpy(&entry, &symbol, sizeof(entry));
  return OB_SUCCESS;
}
#else
typedef void *ModuleHandle;
const ModuleHandle INVALID_MODULE = nullptr;

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

ModuleHandle open_module(const std::string &path, std::string &error)
{
  dlerror();
  ModuleHandle module = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (nullptr == module) {
    const char *message = dlerror();
    error = nullptr == message ? "dlopen failed" : message;
  }
  return module;
}

void close_module(ModuleHandle module)
{
  if (nullptr != module) dlclose(module);
}

int find_entry(ModuleHandle module, seekdb_plugin_entry_v1_fn &entry, std::string &error)
{
  dlerror();
  void *symbol = dlsym(module, PLUGIN_ENTRY_SYMBOL);
  const char *message = dlerror();
  if (nullptr != message) {
    error = message;
    return OB_ENTRY_NOT_EXIST;
  }
  if (nullptr == symbol) {
    error = "plugin entry symbol resolved to null";
    return OB_ENTRY_NOT_EXIST;
  }
  if (sizeof(symbol) != sizeof(entry)) {
    error = "platform function pointer representation is unsupported";
    return OB_NOT_SUPPORTED;
  }
  std::memcpy(&entry, &symbol, sizeof(entry));
  return OB_SUCCESS;
}
#endif

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

} // namespace

struct ObPluginLoader::Impl
{
  struct Module
  {
    Module()
        : plugin_id_(), canonical_path_(), version_(), generation_(), handle_(INVALID_MODULE),
          manifest_(nullptr), verified_artifact_(), owner_(), host_(), instance_(nullptr),
          dependencies_(), dependency_slots_(), last_error_(), initialized_(false), started_(false)
    {
      std::memset(&version_, 0, sizeof(version_));
    }

    std::string plugin_id_;
    std::string canonical_path_;
    seekdb_plugin_semantic_version_t version_;
    uint64_t generation_;
    ModuleHandle handle_;
    const seekdb_plugin_manifest_v1_t *manifest_;
    std::unique_ptr<ObPluginVerifiedArtifact> verified_artifact_;
    std::shared_ptr<ObPluginGeneration> owner_;
    HostContext host_;
    seekdb_plugin_instance_handle_t *instance_;
    std::vector<std::unique_ptr<ObPluginLease> > dependencies_;
    std::vector<const void **> dependency_slots_;
    std::string last_error_;
    bool initialized_;
    bool started_;
  };

  Impl()
      : mutex_(), trusted_directory_(), verifier_(), disable_guard_(),
        registry_(), initialized_(false), shutting_down_(false), loading_(false),
        shutdown_running_(false), terminal_completed_(false), next_generation_(1),
        modules_(), active_(), disabling_(), last_error_()
  {}

  mutable std::mutex mutex_;
  std::string trusted_directory_;
  std::shared_ptr<const ObPluginVerifier> verifier_;
  std::shared_ptr<const ObPluginDisableGuard> disable_guard_;
  std::shared_ptr<ObPluginServiceRegistry> registry_;
  bool initialized_;
  bool shutting_down_;
  bool loading_;
  bool shutdown_running_;
  bool terminal_completed_;
  uint64_t next_generation_;
  std::vector<std::unique_ptr<Module> > modules_;
  std::map<std::string, Module *> active_;
  std::map<std::string, uint64_t> disabling_;
  std::string last_error_;

  static void fill_status(const Module &module, ObPluginStatusSnapshot &status)
  {
    status.plugin_id_ = module.plugin_id_;
    status.canonical_path_ = module.canonical_path_;
    status.version_ = module.version_;
    status.generation_ = module.generation_;
    status.state_ = module.owner_ ? module.owner_->state() : ObPluginState::FAILED;
    status.lease_count_ = module.owner_ ? module.owner_->lease_count() : 0;
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
               (manifest->capabilities & ~KNOWN_CAPABILITIES) != 0 ||
               !all_zero(manifest->reserved,
                         sizeof(manifest->reserved) / sizeof(manifest->reserved[0]))) {
      ret = OB_INVALID_DATA;
      error = "plugin lifecycle or reserved manifest fields are invalid";
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
      if (require.struct_size < descriptor_size || !valid_identifier(require.service_id) ||
          !valid_range(require.version_range) ||
          require.version_range.minimum_inclusive.major == 0 || require.optional > 1 ||
          (require.required_capabilities & ~KNOWN_CAPABILITIES) != 0 ||
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

  int resolve_dependencies(Module &module, std::string &error)
  {
    int ret = OB_SUCCESS;
    try {
      module.dependencies_.reserve(module.manifest_->required_services_count);
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
        if (OB_SUCCESS == acquire_ret) {
          seekdb_plugin_semantic_version_t actual = {
              require.version_range.minimum_inclusive.major, lease->service_minor(),
              lease->service_patch()};
          if (!version_in_range(actual, require.version_range)) {
            acquire_ret = OB_ENTRY_NOT_EXIST;
          }
        }
        if (OB_SUCCESS != acquire_ret) {
          lease->reset();
          if (!require.optional) {
            ret = acquire_ret;
            error = std::string("required service is unavailable: ") + require.service_id;
          }
        } else {
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
                      const ObPluginTerminalStopAuthority *terminal_authority,
                      ObPluginRuntimeDisableResult &result,
                      std::string &error)
  {
    int ret = OB_SUCCESS;
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
      result.phase_ = ObPluginDisablePhase::STOP;
      result.stop_entered_ = true;
      const int stop_ret = call_lifecycle(module.manifest_->stop, module.instance_);
      if (OB_SUCCESS == stop_ret) {
        module.started_ = false;
      } else {
        ret = stop_ret;
        assign_error_noexcept(
            error, "plugin stop callback failed; module is blocked until process-exit retry");
        if (module.owner_->state() != ObPluginState::BLOCKED &&
            OB_SUCCESS != module.owner_->transition_to(ObPluginState::BLOCKED)) {
          append_error_noexcept(error, "; failed to record blocked runtime state");
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

ObPluginArtifactMetadata::ObPluginArtifactMetadata()
    : plugin_id_(), build_id_(), package_version_(), catalog_version_(0),
      data_format_version_(0)
{
  std::memset(&package_version_, 0, sizeof(package_version_));
}

ObPluginStatusSnapshot::ObPluginStatusSnapshot()
    : plugin_id_(), canonical_path_(), version_(), generation_(0),
      state_(ObPluginState::DISCOVERED), lease_count_(0), last_error_()
{
  std::memset(&version_, 0, sizeof(version_));
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
                         const std::shared_ptr<const ObPluginDisableGuard> &disable_guard,
                         const std::shared_ptr<ObPluginServiceRegistry> &registry)
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
    } else if (!verifier || !disable_guard || !registry ||
               trusted_directory.empty()) {
      ret = OB_INVALID_ARGUMENT;
      error = "trusted directory, verifier, disable guard and registry are mandatory";
    } else if (OB_SUCCESS !=
               (ret = canonical_existing(trusted_directory, true, canonical, error))) {
    } else {
      impl_->trusted_directory_ = canonical;
      impl_->verifier_ = verifier;
      impl_->disable_guard_ = disable_guard;
      impl_->registry_ = registry;
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

int ObPluginLoader::load(const std::string &relative_path, uint64_t *loaded_generation)
{
  if (!impl_)
    return OB_ALLOCATE_MEMORY_FAILED;
  int ret = OB_SUCCESS;
  std::string error;
  std::string canonical;
  std::string trusted_directory;
  std::shared_ptr<const ObPluginVerifier> verifier;
  std::unique_ptr<Impl::Module> module;
  std::vector<StagedService> manifest_services;
  ObPluginRegistration publication;
  seekdb_plugin_entry_v1_fn entry = nullptr;
  bool publication_open = false;
  bool active_placeholder = false;
  bool load_reserved = false;

  try {
    if (nullptr != loaded_generation)
      *loaded_generation = 0;
    {
      std::lock_guard<std::mutex> guard(impl_->mutex_);
      if (!impl_->initialized_) {
        ret = OB_NOT_INIT;
        error = "plugin loader is not initialized";
      } else if (impl_->shutting_down_) {
        ret = OB_STATE_NOT_MATCH;
        error = "plugin loader is shutting down";
      } else if (impl_->loading_ || !impl_->disabling_.empty()) {
        ret = OB_EAGAIN;
        error = "another plugin management operation is in progress";
      } else if (!safe_relative_path(relative_path)) {
        ret = OB_INVALID_ARGUMENT;
        error = "plugin path must be a traversal-free relative path";
      } else {
        trusted_directory = impl_->trusted_directory_;
        verifier = impl_->verifier_;
        impl_->loading_ = true;
        load_reserved = true;
      }
    }
    if (OB_SUCCESS == ret) {
      module.reset(new Impl::Module());
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
    if (OB_SUCCESS == ret) {
      module->plugin_id_ = module->manifest_->plugin_id;
      module->version_ = module->manifest_->version;
    }
    if (OB_SUCCESS == ret) {
      std::lock_guard<std::mutex> guard(impl_->mutex_);
      if (!impl_->initialized_ || impl_->shutting_down_ || !impl_->loading_) {
        ret = OB_STATE_NOT_MATCH;
        error = "plugin loader entered terminal shutdown during load";
      } else if (impl_->active_.count(module->plugin_id_) != 0) {
        ret = OB_ENTRY_EXIST;
        error = "a generation of this plugin is already resident";
      } else {
        // Allocate both success and failure bookkeeping before any plugin
        // callback.  Consequently a stop failure can always retain the complete
        // runtime domain without allocating memory in the rollback path.
        impl_->modules_.reserve(impl_->modules_.size() + 1);
        const auto inserted = impl_->active_.insert(
            std::make_pair(module->plugin_id_, static_cast<Impl::Module *>(nullptr)));
        if (!inserted.second) {
          ret = OB_ENTRY_EXIST;
          error = "a generation of this plugin became resident concurrently";
        } else {
          active_placeholder = true;
        }
        if (OB_SUCCESS == ret) {
          if (0 == impl_->next_generation_) {
            ret = OB_SIZE_OVERFLOW;
            error = "plugin generation space is exhausted";
          } else {
            module->generation_ = impl_->next_generation_++;
            module->owner_.reset(
                new ObPluginGeneration(module->plugin_id_, module->generation_));
          }
        }
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
      ret = impl_->resolve_dependencies(*module, error);

      if (OB_SUCCESS == ret) {
        ret = impl_->registry_->begin_registration(module->owner_, publication);
        publication_open = OB_SUCCESS == ret;
        if (OB_SUCCESS != ret)
          error = "cannot begin atomic service publication";
      }
      for (size_t i = 0; OB_SUCCESS == ret && i < manifest_services.size(); ++i) {
        const StagedService &service = manifest_services[i];
        ret = publication.add_service(service.spec_.name_.c_str(), service.spec_.abi_major_,
                                      service.spec_.abi_minor_, service.spec_.abi_patch_,
                                      service.spec_.capabilities_, service.spec_.service_);
        if (OB_SUCCESS != ret)
          error = "manifest service conflicts with the registry";
      }
      if (OB_SUCCESS == ret) {
        ret = module->owner_->transition_to(ObPluginState::INITIALIZING);
        if (OB_SUCCESS != ret)
          error = "failed to enter initializing state";
      }
      if (OB_SUCCESS == ret) {
        module->host_.accepting_registrations_ = true;
        ret = call_lifecycle_init(module->manifest_->init, &module->host_.api_, &module->instance_);
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
        module->started_ = true;
        ret = call_lifecycle(module->manifest_->start, module->instance_);
        if (OB_SUCCESS != ret)
          error = "plugin start callback failed";
      }
      {
        std::lock_guard<std::mutex> host_guard(module->host_.mutex_);
        module->host_.accepting_registrations_ = false;
        if (OB_SUCCESS == ret && !module->host_.registrations_.empty()) {
          ret = OB_STATE_NOT_MATCH;
          error = "plugin left a registration transaction open";
        }
      }
      for (size_t i = 0; OB_SUCCESS == ret && i < module->host_.staged_.size(); ++i) {
        const StagedService &service = module->host_.staged_[i];
        ret = publication.add_service(service.spec_.name_.c_str(), service.spec_.abi_major_,
                                      service.spec_.abi_minor_, service.spec_.abi_patch_,
                                      service.spec_.capabilities_, service.spec_.service_);
        if (OB_SUCCESS != ret)
          error = "dynamic service conflicts with manifest or registry";
      }
      if (OB_SUCCESS == ret) {
        std::lock_guard<std::mutex> guard(impl_->mutex_);
        if (!impl_->initialized_ || impl_->shutting_down_ || !impl_->loading_) {
          ret = OB_STATE_NOT_MATCH;
          error = "plugin loader entered terminal shutdown before publication";
        } else {
          ret = publication.commit();
          if (OB_SUCCESS == ret) publication_open = false;
          if (OB_SUCCESS != ret) {
            error = "atomic service publication failed";
          } else {
            module->host_.staged_.clear();
            Impl::Module *active = module.get();
            impl_->active_.find(module->plugin_id_)->second = active;
            if (nullptr != loaded_generation)
              *loaded_generation = module->generation_;
            impl_->modules_.push_back(std::move(module));
            impl_->loading_ = false;
            load_reserved = false;
            impl_->set_error(std::string());
          }
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

  if (OB_SUCCESS != ret) {
    if (publication_open)
      publication.rollback();
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
        close_module(module->handle_);
        module->handle_ = INVALID_MODULE;
        module->manifest_ = nullptr;
      }
    }
    std::lock_guard<std::mutex> guard(impl_->mutex_);
    if (module && active_placeholder) {
      if (safe_to_teardown) {
        impl_->active_.erase(module->plugin_id_);
      } else {
        // A callback may still be running.  Keep the plugin ID occupied by
        // this failed generation so a second instance cannot be started.
        impl_->active_.find(module->plugin_id_)->second = module.get();
      }
    }
    if (module && module->owner_) {
      impl_->modules_.push_back(std::move(module));
    }
    if (load_reserved) {
      impl_->loading_ = false;
      load_reserved = false;
    }
    impl_->set_error(error);
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
      (void)impl_->disable_runtime(*module, drain_timeout_us, false, nullptr,
                                   runtime_result, runtime_error);
    } else if (nullptr != module && module->owner_) {
      runtime_result.actual_state_ = module->owner_->state();
      assign_error_noexcept(runtime_result.error_, runtime_error.c_str());
    }
  }

  int finish_ret = OB_SUCCESS;
  if (permit) {
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
      ret = impl_->disable_runtime(module, drain_timeout_us, true,
                                   &terminal_authority,
                                   runtime_result, error);
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
    for (size_t index = 0; index < impl_->modules_.size(); ++index) {
      ModuleHandle handle = INVALID_MODULE;
      {
        std::lock_guard<std::mutex> guard(impl_->mutex_);
        Impl::Module &module = *impl_->modules_[index];
        handle = module.handle_;
        module.handle_ = INVALID_MODULE;
        module.manifest_ = nullptr;
      }
      if (INVALID_MODULE != handle) close_module(handle);
    }
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

std::string ObPluginLoader::trusted_directory() const
{
  if (!impl_) return std::string();
  std::lock_guard<std::mutex> guard(impl_->mutex_);
  return impl_->trusted_directory_;
}

} // namespace plugin
} // namespace share
} // namespace oceanbase
