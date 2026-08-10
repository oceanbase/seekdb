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

#define USING_LOG_PREFIX SERVER

#include "observer/ob_server_plugin_runtime.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <new>
#include <sstream>
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
#include "lib/oblog/ob_log.h"
#include "lib/string/ob_sql_string.h"
#include "common/mysqlclient/ob_isql_client.h"

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

std::string trim_copy(const std::string &value)
{
  size_t begin = 0;
  while (begin < value.size() &&
         std::isspace(static_cast<unsigned char>(value[begin]))) {
    ++begin;
  }
  size_t end = value.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(begin, end - begin);
}

bool parse_manifest_value(const std::string &line,
                          const char *key,
                          std::string &value)
{
  const std::string trimmed = trim_copy(line);
  const size_t separator = trimmed.find('=');
  if (separator == std::string::npos ||
      trim_copy(trimmed.substr(0, separator)) != key) {
    return false;
  }
  std::string parsed = trim_copy(trimmed.substr(separator + 1));
  if (parsed.size() >= 2 && parsed.front() == '"' && parsed.back() == '"') {
    parsed = parsed.substr(1, parsed.size() - 2);
  }
  value = parsed;
  return true;
}

bool parse_uint32_value(const std::string &text, uint32_t &value)
{
  try {
    size_t consumed = 0;
    const unsigned long parsed = std::stoul(text, &consumed, 10);
    if (consumed != text.size() || parsed > UINT32_MAX) return false;
    value = static_cast<uint32_t>(parsed);
    return true;
  } catch (...) {
    return false;
  }
}

bool parse_semantic_version(const std::string &text,
                            seekdb_plugin_semantic_version_t &version)
{
  std::stringstream stream(text);
  std::string component;
  uint32_t values[3] = {0, 0, 0};
  for (size_t i = 0; i < 3; ++i) {
    if (!std::getline(stream, component, '.') ||
        !parse_uint32_value(component, values[i])) {
      return false;
    }
  }
  if (std::getline(stream, component, '.')) return false;
  version.major = values[0];
  version.minor = values[1];
  version.patch = values[2];
  return version.major != 0;
}

// Locate one package without mutating the durable catalog.  The package
// directory is merely a candidate source; INSTALL PLUGIN is the operation
// which pins it and changes desired state.
int find_plugin_package(const std::string &trusted_directory,
                        const std::string &plugin_name,
                        const std::string &soname,
                        share::plugin::ObPluginPackageInstallSpec &spec,
                        std::string &error)
{
  namespace fs = std::filesystem;
  int ret = OB_ENTRY_NOT_EXIST;
  error.clear();
  try {
    const fs::path root(trusted_directory);
    std::error_code ec;
    for (const fs::directory_entry &entry : fs::directory_iterator(root, ec)) {
      if (ec) break;
      if (!entry.is_directory(ec)) { ec.clear(); continue; }
      const fs::path manifest_path = entry.path() / "plugin.toml";
      if (!fs::is_regular_file(manifest_path, ec)) { ec.clear(); continue; }
      std::ifstream manifest(manifest_path);
      if (!manifest.is_open()) continue;
      std::string id, version, build, entrypoint, catalog_version, format_version, line;
      while (std::getline(manifest, line)) {
        const size_t comment = line.find('#');
        if (comment != std::string::npos) line.resize(comment);
        (void)parse_manifest_value(line, "plugin_id", id);
        (void)parse_manifest_value(line, "package_version", version);
        (void)parse_manifest_value(line, "build_id", build);
        (void)parse_manifest_value(line, "entrypoint", entrypoint);
        (void)parse_manifest_value(line, "catalog_schema_version", catalog_version);
        (void)parse_manifest_value(line, "data_format_version", format_version);
      }
      const fs::path rel = (entry.path() / fs::path(entrypoint)).lexically_relative(root);
      const std::string rel_name = rel.generic_string();
      const bool name_matches = plugin_name == id ||
          (id.size() > plugin_name.size() &&
           id.compare(id.size() - plugin_name.size(), plugin_name.size(), plugin_name) == 0 &&
           id[id.size() - plugin_name.size() - 1] == '.');
      const bool soname_matches = soname == rel_name || soname == entrypoint;
      if (!name_matches || !soname_matches || rel_name.empty() ||
          rel_name == ".." || rel_name.rfind("../", 0) == 0 ||
          fs::path(entrypoint).is_absolute() ||
          !fs::is_regular_file(root / fs::path(rel_name), ec)) {
        ec.clear();
        continue;
      }
      spec = share::plugin::ObPluginPackageInstallSpec();
      spec.relative_path_ = rel_name;
      spec.artifact_.plugin_id_ = id;
      spec.artifact_.build_id_ = build;
      if (!parse_semantic_version(version, spec.artifact_.package_version_) ||
          !parse_uint32_value(catalog_version, spec.artifact_.catalog_version_) ||
          !parse_uint32_value(format_version, spec.artifact_.data_format_version_)) {
        ret = OB_INVALID_ARGUMENT;
        error = "plugin manifest has invalid identity fields '" + manifest_path.string() + "'";
        break;
      }
      spec.artifact_.package_digest_ =
          "sha256:0000000000000000000000000000000000000000000000000000000000000000";
      spec.verification_level_ = share::plugin::ObPluginVerificationLevel::IDENTITY_PINNED;
      spec.operator_id_ = "sql.install_plugin";
      spec.audit_id_ = "sql.install." + id;
      ret = OB_SUCCESS;
      break;
    }
    if (OB_ENTRY_NOT_EXIST == ret) {
      error = "no plugin package matches PLUGIN '" + plugin_name +
              "' SONAME '" + soname + "'";
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    error = "plugin package lookup allocation failed";
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
    error = "unexpected plugin package lookup failure";
  }
  return ret;
}

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

int ObServerPluginRuntime::init(common::ObISQLClient *sql_client,
                                const std::string &trusted_directory)
{
  int ret = OB_SUCCESS;
  try {
    if (!impl_) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (impl_->initialized_) {
      ret = OB_INIT_TWICE;
    } else if (nullptr == sql_client) {
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
      } else if (OB_FAIL(catalog->init(sql_client))) {
      } else {
        // Discovery is deferred until the server-ready gate. At this point
        // init_sql_proxy/schema bootstrap has completed and catalog writes can
        // safely use the normal seekdb system-catalog transaction.
        if (OB_SUCC(ret)) {
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

    // Startup only restores rows previously persisted by INSTALL PLUGIN.
    // Packages found on disk are candidates until an administrator explicitly
    // installs them; this matches MySQL/Percona lifecycle semantics.

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

int ObServerPluginRuntime::install_plugin(const std::string &plugin_name,
                                          const std::string &soname,
                                          std::string &error)
{
  int ret = OB_SUCCESS;
  error.clear();
#if defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
  try {
    if (!impl_ || !impl_->initialized_ || !impl_->catalog_ || !impl_->loader_) {
      ret = OB_NOT_INIT;
      error = "plugin runtime is not initialized";
    } else {
      LOG_INFO("explicit plugin install begin",
               K(plugin_name.c_str()), K(soname.c_str()));
      share::plugin::ObPluginPackageInstallSpec spec;
      if (OB_FAIL(find_plugin_package(impl_->trusted_directory_, plugin_name,
                                      soname, spec, error))) {
        LOG_WARN("explicit plugin package lookup failed", K(ret), KCSTRING(error.c_str()));
      } else if (OB_FAIL(impl_->catalog_->install_package(spec, error))) {
        LOG_WARN("explicit plugin catalog install failed", K(ret), KCSTRING(error.c_str()));
      } else {
        LOG_INFO("explicit plugin catalog install committed",
                 K(spec.artifact_.plugin_id_.c_str()), K(spec.relative_path_.c_str()));
        uint64_t generation = 0;
        ret = impl_->loader_->load(spec.relative_path_, &generation);
        if (OB_SUCCESS != ret) {
          error = impl_->loader_->last_error();
          if (error.empty()) error = "plugin activation failed";
          LOG_ERROR("explicit plugin activation failed", K(ret), KCSTRING(error.c_str()));
        } else {
          LOG_INFO("explicit plugin activation succeeded",
                   K(spec.artifact_.plugin_id_.c_str()), K(generation));
        }
      }
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    error = "plugin install allocation failed";
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
    error = "unexpected plugin install failure";
  }
#else
  UNUSED(plugin_name);
  UNUSED(soname);
  ret = OB_NOT_SUPPORTED;
  error = "experimental plugin support is disabled";
#endif
  return ret;
}

int ObServerPluginRuntime::uninstall_plugin(const std::string &plugin_name,
                                            std::string &error)
{
  int ret = OB_SUCCESS;
  error.clear();
#if defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
  try {
    if (!impl_ || !impl_->initialized_ || !impl_->catalog_ || !impl_->loader_) {
      ret = OB_NOT_INIT;
      error = "plugin runtime is not initialized";
    } else {
      std::string plugin_id = plugin_name;
      share::plugin::ObPluginCatalogRecord record;
      ret = impl_->catalog_->get_record(plugin_id, record);
      if (OB_ENTRY_NOT_EXIST == ret) {
        std::vector<share::plugin::ObPluginCatalogRecord> records;
        ret = impl_->catalog_->list_records(records);
        for (const auto &candidate : records) {
          if (candidate.plugin_id_ == plugin_name ||
              (candidate.plugin_id_.size() > plugin_name.size() &&
               candidate.plugin_id_.compare(candidate.plugin_id_.size() - plugin_name.size(),
                                            plugin_name.size(), plugin_name) == 0 &&
               candidate.plugin_id_[candidate.plugin_id_.size() - plugin_name.size() - 1] == '.')) {
            plugin_id = candidate.plugin_id_;
            record = candidate;
            ret = OB_SUCCESS;
            break;
          }
        }
      }
      if (OB_SUCCESS == ret && record.actual_state_ == share::plugin::ObPluginState::ACTIVE) {
        ret = impl_->loader_->disable(plugin_id, 30L * 1000L * 1000L);
        if (OB_ENTRY_NOT_EXIST == ret) ret = OB_SUCCESS;
        if (OB_SUCCESS != ret) {
          error = impl_->loader_->last_error();
          if (error.empty()) error = "plugin disable failed";
        }
      }
      if (OB_SUCCESS == ret) {
        std::vector<share::plugin::ObPluginRestrictBlocker> blockers;
        ret = impl_->catalog_->uninstall_restrict(
            plugin_id, "sql.uninstall_plugin", "sql.uninstall." + plugin_id,
            blockers, error);
      }
      if (OB_SUCCESS == ret) error.clear();
      if (OB_ENTRY_NOT_EXIST == ret && error.empty()) {
        error = "plugin is not installed: " + plugin_name;
      }
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    error = "plugin uninstall allocation failed";
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
    error = "unexpected plugin uninstall failure";
  }
#else
  UNUSED(plugin_name);
  ret = OB_NOT_SUPPORTED;
  error = "experimental plugin support is disabled";
#endif
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
