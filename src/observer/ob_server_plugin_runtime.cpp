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

#include "observer/ob_server_plugin_runtime.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <new>
#ifdef _WIN32
#include <windows.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <vector>

#include "lib/ob_errno.h"
#include "lib/file/file_directory_utils.h"
#include "lib/string/ob_sql_string.h"
#include "share/storage/ob_sqlite_connection_pool.h"

#if defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
#include "seekdb/plugin/execution_spi.h"
#include "share/plugin/ob_plugin_catalog.h"
#include "share/plugin/ob_plugin_loader.h"
#include "share/plugin/ob_plugin_registry.h"
#endif

namespace oceanbase
{
namespace observer
{

using namespace common;

namespace
{

#if defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)

std::string canonical_candidate(const std::string &path);

class CatalogVerifiedArtifact final : public share::plugin::ObPluginVerifiedArtifact
{
public:
  CatalogVerifiedArtifact(const std::string &path,
                          const share::plugin::ObPluginArtifactMetadata &metadata)
      : path_(path), metadata_(metadata)
  {}

  const std::string &load_path() const override { return path_; }
  const share::plugin::ObPluginArtifactMetadata &metadata() const override
  {
    return metadata_;
  }

private:
  std::string path_;
  share::plugin::ObPluginArtifactMetadata metadata_;
};

// Phase-1 local verifier: the durable catalog is the local installation
// authority. No signature, trust-chain or content-hash verification is done.
class CatalogIdentityVerifier final : public share::plugin::ObPluginVerifier
{
public:
  CatalogIdentityVerifier(share::plugin::ObPluginCatalog *catalog,
                          const std::string &trusted_directory)
      : catalog_(catalog), trusted_directory_(trusted_directory)
  {}

  int verify_and_pin(const std::string &canonical_path,
                    std::unique_ptr<share::plugin::ObPluginVerifiedArtifact> &artifact,
                    std::string &error) const override
  {
    int ret = OB_SUCCESS;
    artifact.reset();
    error.clear();
    try {
      if (nullptr == catalog_ || trusted_directory_.empty()) {
        ret = OB_NOT_INIT;
        error = "local plugin verifier is not initialized";
      } else {
        std::vector<share::plugin::ObPluginCatalogRecord> records;
        if (OB_FAIL(catalog_->list_records(records))) {
          error = "failed to read local plugin catalog";
        } else {
          const std::string prefix = trusted_directory_ + "/";
          for (const auto &record : records) {
            if (record.relative_path_.empty()) continue;
            const std::string candidate = prefix + record.relative_path_;
            const std::string resolved = canonical_candidate(candidate);
            if (!resolved.empty()) {
              const bool match = canonical_path == resolved;
              if (match) {
                share::plugin::ObPluginArtifactMetadata metadata;
                metadata.plugin_id_ = record.plugin_id_;
                metadata.build_id_ = record.build_id_;
                metadata.package_digest_ = record.package_digest_;
                metadata.package_version_ = record.package_version_;
                metadata.catalog_version_ = record.catalog_version_;
                metadata.data_format_version_ = record.data_format_version_;
                artifact.reset(new (std::nothrow)
                                   CatalogVerifiedArtifact(canonical_path, metadata));
                if (nullptr == artifact) {
                  ret = OB_ALLOCATE_MEMORY_FAILED;
                  error = "local plugin verifier allocation failed";
                }
                break;
              }
            }
          }
          if (OB_SUCCESS == ret && !artifact) {
            ret = OB_ENTRY_NOT_EXIST;
            error = "plugin artifact is not installed in the local catalog";
          }
        }
      }
    } catch (const std::bad_alloc &) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      error = "local plugin verifier allocation failed";
    } catch (...) {
      ret = OB_ERR_UNEXPECTED;
      error = "unexpected local plugin verification failure";
    }
    return ret;
  }

private:
  share::plugin::ObPluginCatalog *catalog_;
  std::string trusted_directory_;
};

bool regular_file_exists(const std::string &path)
{
#ifdef _WIN32
  struct _stat info;
  return 0 == ::_stat(path.c_str(), &info) && (info.st_mode & _S_IFREG) != 0;
#else
  struct stat info;
  return 0 == ::stat(path.c_str(), &info) && S_ISREG(info.st_mode);
#endif
}

std::string canonical_candidate(const std::string &path)
{
#ifdef _WIN32
  char buffer[MAX_PATH] = {0};
  const DWORD length = ::GetFullPathNameA(path.c_str(), MAX_PATH, buffer, nullptr);
  if (0 == length || length >= MAX_PATH) return std::string();
  std::string result(buffer, length);
  std::replace(result.begin(), result.end(), '/', '\\');
  std::transform(result.begin(), result.end(), result.begin(), [](const char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  });
  return result;
#else
  char *resolved = ::realpath(path.c_str(), nullptr);
  if (nullptr == resolved) return std::string();
  std::string result(resolved);
  std::free(resolved);
  return result;
#endif
}

#endif

void set_bridge_error_noexcept(std::string &error, const char *message) noexcept
{
  try {
    error.assign(message);
  } catch (...) {
    error.clear();
  }
}

} // namespace

struct ObServerPluginRuntime::Impl
{
  Impl()
      : initialized_(false)
#if defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
      , catalog_(), registry_(), verifier_(), loader_(), trusted_directory_()
#endif
  {}

  bool initialized_;
#if defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
  std::unique_ptr<share::plugin::ObPluginCatalog> catalog_;
  std::shared_ptr<share::plugin::ObPluginServiceRegistry> registry_;
  std::shared_ptr<const share::plugin::ObPluginVerifier> verifier_;
  std::unique_ptr<share::plugin::ObPluginLoader> loader_;
  std::string trusted_directory_;
#endif
};

ObServerPluginRuntime::ObServerPluginRuntime()
    : impl_(new (std::nothrow) Impl())
{}

ObServerPluginRuntime::~ObServerPluginRuntime()
{
  destroy();
}

int ObServerPluginRuntime::init(share::ObSQLiteConnectionPool *meta_db_pool,
                                const std::string &trusted_directory)
{
  int ret = OB_SUCCESS;
  try {
    if (!impl_) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (impl_->initialized_) {
      ret = OB_INIT_TWICE;
    } else if (nullptr == meta_db_pool || !meta_db_pool->is_inited()) {
      ret = OB_INVALID_ARGUMENT;
    }

#if defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
    if (OB_SUCC(ret)) {
      const std::string root = trusted_directory.empty() ? "./plugins" : trusted_directory;
      ObSqlString absolute_root;
      if (OB_FAIL(absolute_root.assign(root.c_str()))) {
      } else if (OB_FAIL(FileDirectoryUtils::create_full_path(root.c_str()))) {
      } else if (OB_FAIL(FileDirectoryUtils::to_absolute_path(absolute_root))) {
      } else {
        impl_->trusted_directory_.assign(absolute_root.ptr(), absolute_root.length());
      }
    }
    if (OB_SUCC(ret)) {
      std::unique_ptr<share::plugin::ObPluginCatalog> catalog(
          new (std::nothrow) share::plugin::ObPluginCatalog());
      if (!catalog) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (OB_FAIL(catalog->init(meta_db_pool))) {
      } else {
        std::shared_ptr<share::plugin::ObPluginServiceRegistry> registry(
            new (std::nothrow) share::plugin::ObPluginServiceRegistry());
        std::shared_ptr<const share::plugin::ObPluginVerifier> verifier(
            new (std::nothrow) CatalogIdentityVerifier(
                catalog.get(), impl_->trusted_directory_));
        std::shared_ptr<const share::plugin::ObPluginActivationGuard> activation_guard(
            catalog.get(), [](const share::plugin::ObPluginActivationGuard *) {});
        std::shared_ptr<const share::plugin::ObPluginDisableGuard> disable_guard(
            catalog.get(), [](const share::plugin::ObPluginDisableGuard *) {});
        std::unique_ptr<share::plugin::ObPluginLoader> loader(
            new (std::nothrow) share::plugin::ObPluginLoader());
        if (!registry || !verifier || !loader) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else if (OB_FAIL(loader->init(impl_->trusted_directory_, verifier,
                                         activation_guard, disable_guard,
                                         registry))) {
        } else {
          impl_->registry_ = std::move(registry);
          impl_->verifier_ = std::move(verifier);
          impl_->loader_ = std::move(loader);
          impl_->catalog_ = std::move(catalog);
          impl_->initialized_ = true;
        }
      }
    }
#else
    if (OB_SUCC(ret)) {
      impl_->initialized_ = true;
    }
#endif
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  if (OB_SUCCESS != ret && impl_ && !impl_->initialized_) {
#if defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
    if (impl_->loader_) {
      (void)impl_->loader_->shutdown_for_process_exit(0);
      impl_->loader_.reset();
    }
    impl_->verifier_.reset();
    impl_->registry_.reset();
    impl_->catalog_.reset();
#endif
  }
  return ret;
}

int ObServerPluginRuntime::recover_before_server_ready(std::string &error)
{
  int ret = OB_SUCCESS;
  try {
    error.clear();
    if (!impl_ || !impl_->initialized_) {
      ret = OB_NOT_INIT;
      error = "server plugin runtime bridge is not initialized";
    }

#if defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
    if (OB_SUCC(ret) && !impl_->catalog_) {
      ret = OB_NOT_INIT;
      error = "plugin catalog is not initialized";
    }

    // Keep blocked and missing-artifact failures read-only. This preserves the
    // durable evidence while still allowing an installed local package to be
    // replayed by the real loader below.
    std::vector<share::plugin::ObPluginCatalogRecord> records;
    if (OB_SUCC(ret) && OB_FAIL(impl_->catalog_->list_records(records))) {
      error = "failed to inspect durable plugin state before server ready";
    }

    const share::plugin::ObPluginCatalogRecord *blocked = nullptr;
    const share::plugin::ObPluginCatalogRecord *missing = nullptr;
    for (size_t i = 0; OB_SUCC(ret) && i < records.size(); ++i) {
      const share::plugin::ObPluginCatalogRecord &record = records[i];
      if (nullptr == blocked &&
          record.actual_state_ == share::plugin::ObPluginState::BLOCKED) {
        blocked = &record;
      }
      if (nullptr == missing &&
          record.desired_state_ == share::plugin::ObPluginDesiredState::ACTIVE) {
        const std::string path = impl_->trusted_directory_ + "/" + record.relative_path_;
        if (!regular_file_exists(path)) missing = &record;
      }
    }

    if (OB_SUCC(ret) && nullptr != blocked) {
      ret = OB_STATE_NOT_MATCH;
      error = "plugin '" + blocked->plugin_id_ +
              "' is BLOCKED and requires explicit administrator recovery";
    } else if (OB_SUCC(ret) && nullptr != missing) {
      ret = OB_NOT_SUPPORTED;
      error = "plugin '" + missing->plugin_id_ +
              "' is ACTIVE but its local artifact is unavailable";
    }

    if (OB_SUCC(ret) && !impl_->loader_) {
      ret = OB_NOT_INIT;
      error = "plugin loader is not initialized";
    } else if (OB_SUCC(ret)) {
      share::plugin::ObPluginStartupReport report;
      if (OB_FAIL(impl_->catalog_->recover_before_server_ready(
              *impl_->loader_, report, error))) {
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(impl_->catalog_->check_server_ready(error))) {
      if (error.empty()) {
        error = "durable plugin state is not safe for server ready";
      }
    }
#endif
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    set_bridge_error_noexcept(
        error, "plugin server-ready bridge allocation failed");
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
    set_bridge_error_noexcept(
        error, "unexpected plugin server-ready bridge failure");
  }
  return ret;
}

int ObServerPluginRuntime::execute_function(
    const char *service_id,
    const uint32_t abi_major,
    const uint32_t required_minor,
    const seekdb_plugin_execution_context_v1 *context,
    const seekdb_plugin_execution_value_v1 *arguments,
    const uint32_t argument_count)
{
  if (!impl_ || !impl_->initialized_) return OB_NOT_INIT;
#if defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
  if (!impl_->loader_) return OB_NOT_INIT;
  return impl_->loader_->execute_function(service_id, abi_major, required_minor,
                                          context, arguments, argument_count);
#else
  UNUSED(service_id);
  UNUSED(abi_major);
  UNUSED(required_minor);
  UNUSED(context);
  UNUSED(arguments);
  UNUSED(argument_count);
  return OB_NOT_SUPPORTED;
#endif
}

int ObServerPluginRuntime::execute_extension(
    const seekdb_plugin_extension_kind_t kind,
    const char *sql_name,
    const seekdb_plugin_execution_context_v1 *context,
    const seekdb_plugin_execution_value_v1 *arguments,
    const uint32_t argument_count)
{
  if (!impl_ || !impl_->initialized_) return OB_NOT_INIT;
#if defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
  if (!impl_->loader_) return OB_NOT_INIT;
  return impl_->loader_->execute_extension(
      kind, sql_name,
      reinterpret_cast<const seekdb_plugin_execution_context_v1_t *>(context),
      reinterpret_cast<const seekdb_plugin_execution_value_v1_t *>(arguments),
      argument_count);
#else
  UNUSED(kind);
  UNUSED(sql_name);
  UNUSED(context);
  UNUSED(arguments);
  UNUSED(argument_count);
  return OB_NOT_SUPPORTED;
#endif
}

void ObServerPluginRuntime::destroy() noexcept
{
  if (impl_) {
#if defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
    // The catalog retains a non-owning meta_db pool pointer.  ObServer calls
    // this before its pool is destroyed; declaration order provides the same
    // guarantee as a final fallback during normal member destruction.
    if (impl_->loader_) {
      (void)impl_->loader_->shutdown_for_process_exit(0);
      impl_->loader_.reset();
    }
    impl_->verifier_.reset();
    impl_->registry_.reset();
    impl_->catalog_.reset();
#endif
    impl_->initialized_ = false;
  }
}

} // namespace observer
} // namespace oceanbase
