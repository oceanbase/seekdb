/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "share/plugin/extension_package.h"
#include "plugin_runtime.h"
#include "lib/ob_errno.h"
#include <memory>
#include <new>

namespace oceanbase { namespace share { namespace plugin {

namespace {
int read_extension_source(const std::string &root, const std::string &name,
                          const std::string *installed_version, const std::string &requested_version,
                          ExtensionPackageSource &source, std::string &error)
{
  using namespace common;
  source = ExtensionPackageSource{};
  error.clear();
  try {
    if (root.size() > 32768 || name.size() > 255 || requested_version.size() > 255 ||
        (installed_version != nullptr && (installed_version->empty() || installed_version->size() > 255))) {
      error = "package root, name or requested version exceeds its size limit";
      return OB_INVALID_ARGUMENT;
    }
    seekdb_runtime_package *raw = nullptr;
    char diagnostic[512] = {};
    const int32_t status = installed_version == nullptr ? seekdb_runtime_package_read(
        reinterpret_cast<const uint8_t *>(root.data()), static_cast<uint32_t>(root.size()),
        reinterpret_cast<const uint8_t *>(name.data()), static_cast<uint32_t>(name.size()),
        reinterpret_cast<const uint8_t *>(requested_version.data()), static_cast<uint32_t>(requested_version.size()),
        &raw, diagnostic, sizeof(diagnostic)) : seekdb_runtime_package_read_update(
        reinterpret_cast<const uint8_t *>(root.data()), static_cast<uint32_t>(root.size()),
        reinterpret_cast<const uint8_t *>(name.data()), static_cast<uint32_t>(name.size()),
        reinterpret_cast<const uint8_t *>(installed_version->data()), static_cast<uint32_t>(installed_version->size()),
        reinterpret_cast<const uint8_t *>(requested_version.data()), static_cast<uint32_t>(requested_version.size()),
        &raw, diagnostic, sizeof(diagnostic));
    std::unique_ptr<seekdb_runtime_package, decltype(&seekdb_runtime_package_destroy)>
        package(raw, seekdb_runtime_package_destroy);
    if (SEEKDB_RUNTIME_OK != status) {
      error = diagnostic;
      return status == SEEKDB_RUNTIME_NO_MEMORY ? OB_ALLOCATE_MEMORY_FAILED :
          status == SEEKDB_RUNTIME_IO_ERROR ? OB_IO_ERROR :
          status == SEEKDB_RUNTIME_NOT_FOUND ? OB_FILE_NOT_EXIST : OB_INVALID_ARGUMENT;
    }
    if (!package) return OB_ERR_UNEXPECTED;
    ExtensionPackageSource staged;
    auto copy = [&](uint32_t field, uint32_t index, std::string &output) {
      const uint8_t *data = nullptr;
      uint32_t length = 0;
      const int32_t result = seekdb_runtime_package_text(package.get(), field, index, &data, &length);
      if (result != SEEKDB_RUNTIME_OK || nullptr == data) return OB_ERR_UNEXPECTED;
      output.assign(reinterpret_cast<const char *>(data), length);
      return OB_SUCCESS;
    };
    int ret = OB_SUCCESS;
    uint32_t count = 0, relocatable = 0;
    if (SEEKDB_RUNTIME_OK != seekdb_runtime_package_info(package.get(), &count, &relocatable) || count > 64 || relocatable > 1) {
      return OB_ERR_UNEXPECTED;
    }
    if (OB_SUCCESS != (ret = copy(SEEKDB_RUNTIME_PACKAGE_NAME, 0, staged.name_)) ||
        OB_SUCCESS != (ret = copy(SEEKDB_RUNTIME_PACKAGE_VERSION, 0, staged.version_)) ||
        OB_SUCCESS != (ret = copy(SEEKDB_RUNTIME_PACKAGE_FROM_VERSION, 0, staged.from_version_)) ||
        OB_SUCCESS != (ret = copy(SEEKDB_RUNTIME_PACKAGE_MODULE, 0, staged.native_module_)) ||
        OB_SUCCESS != (ret = copy(SEEKDB_RUNTIME_PACKAGE_SCHEMA, 0, staged.schema_))) return ret;
    if (staged.from_version_ != (installed_version == nullptr ? std::string() : *installed_version)) return OB_ERR_UNEXPECTED;
    uint32_t script_count = 0;
    if (SEEKDB_RUNTIME_OK != seekdb_runtime_package_script_count(package.get(), &script_count) ||
        script_count > 1024) return OB_ERR_UNEXPECTED;
    const bool no_op = installed_version != nullptr && staged.from_version_ == staged.version_;
    uint32_t native_install = 0;
    if (seekdb_runtime_package_native_install(package.get(), &native_install) != SEEKDB_RUNTIME_OK || native_install > 1)
      return OB_ERR_UNEXPECTED;
    staged.native_install_ = native_install != 0;
    const bool native_base = installed_version == nullptr && staged.native_install_;
    if ((script_count == 0) != (no_op || native_base) ||
        (staged.native_install_ && staged.native_module_.empty())) return OB_ERR_UNEXPECTED;
    size_t total = 0;
    for (uint32_t i = 0; i < script_count; ++i) {
      ExtensionPackageScript script;
      if (OB_SUCCESS != (ret = copy(SEEKDB_RUNTIME_PACKAGE_SCRIPT_SQL, i, script.sql_)) ||
          OB_SUCCESS != (ret = copy(SEEKDB_RUNTIME_PACKAGE_SCRIPT_FROM, i, script.from_version_)) ||
          OB_SUCCESS != (ret = copy(SEEKDB_RUNTIME_PACKAGE_SCRIPT_TO, i, script.to_version_))) return ret;
      if ((script.from_version_.empty() && script.sql_.empty()) || script.sql_.size() > 4 * 1024 * 1024 - total ||
          script.to_version_.empty() ||
          script.from_version_ != (i == 0 ? staged.from_version_ : staged.scripts_.back().to_version_)) {
        return OB_ERR_UNEXPECTED;
      }
      total += script.sql_.size();
      staged.scripts_.push_back(std::move(script));
    }
    if (!no_op && !native_base && staged.scripts_.back().to_version_ != staged.version_) return OB_ERR_UNEXPECTED;
    for (uint32_t i = 0; i < count; ++i) {
      std::string dependency;
      if (OB_SUCCESS != (ret = copy(SEEKDB_RUNTIME_PACKAGE_DEPENDENCY, i, dependency))) return ret;
      staged.requires_.push_back(std::move(dependency));
    }
    staged.relocatable_ = relocatable != 0;
    uint32_t prerequisite_count = 0;
    if (SEEKDB_RUNTIME_OK != seekdb_runtime_package_prerequisite_count(package.get(), &prerequisite_count) ||
        prerequisite_count > 64 - count) return OB_ERR_UNEXPECTED;
    for (uint32_t i = 0; i < prerequisite_count; ++i) {
      std::string dependency;
      if (OB_SUCCESS != (ret = copy(SEEKDB_RUNTIME_PACKAGE_PREREQUISITE, i, dependency))) return ret;
      staged.prerequisites_.push_back(std::move(dependency));
    }
    source = std::move(staged);
    return OB_SUCCESS;
  } catch (const std::bad_alloc &) {
    return OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    return OB_ERR_UNEXPECTED;
  }
}
} // namespace

int read_extension_package(const std::string &root, const std::string &name,
                           const std::string &requested_version,
                           ExtensionPackageSource &source, std::string &error)
{
  return read_extension_source(root, name, nullptr, requested_version, source, error);
}

int read_extension_update(const std::string &root, const std::string &name,
                          const std::string &installed_version, const std::string &requested_version,
                          ExtensionPackageSource &source, std::string &error)
{
  return read_extension_source(root, name, &installed_version, requested_version, source, error);
}

int validate_extension_package_source(const ExtensionPackageSource &source, std::string &error)
{
  using namespace common;
  error.clear();
  try {
    if (source.name_.size() > 255 || source.from_version_.size() > 255 || source.version_.size() > 255 ||
        source.native_module_.size() > 255 || source.schema_.size() > 255 ||
        source.requires_.size() > 64 || source.prerequisites_.size() > 64 - source.requires_.size() || source.scripts_.size() > 1024) {
      error = "in-memory package metadata exceeds host bounds";
      return OB_INVALID_ARGUMENT;
    }
    const auto text = [](const std::string &value) {
      return seekdb_runtime_package_input_text{
          reinterpret_cast<const uint8_t *>(value.data()), static_cast<uint32_t>(value.size())};
    };
    std::vector<seekdb_runtime_package_input_text> dependencies;
    for (const auto &dependency : source.requires_) {
      if (dependency.size() > 255) {
        error = "in-memory dependency name exceeds host bounds";
        return OB_INVALID_ARGUMENT;
      }
      dependencies.push_back(text(dependency));
    }
    std::vector<seekdb_runtime_package_input_script> scripts;
    std::vector<seekdb_runtime_package_input_text> prerequisites;
    for (const auto &dependency : source.prerequisites_) {
      if (dependency.size() > 255) {
        error = "in-memory prerequisite name exceeds host bounds";
        return OB_INVALID_ARGUMENT;
      }
      prerequisites.push_back(text(dependency));
    }
    size_t total = 0;
    for (const auto &script : source.scripts_) {
      if (script.from_version_.size() > 255 || script.to_version_.size() > 255 ||
          script.sql_.size() > 4 * 1024 * 1024 - total) {
        error = "in-memory script metadata or SQL exceeds host bounds";
        return OB_INVALID_ARGUMENT;
      }
      total += script.sql_.size();
      scripts.push_back({text(script.from_version_), text(script.to_version_), text(script.sql_)});
    }
    const seekdb_runtime_package_input_source input{
        sizeof(input), source.relocatable_ ? 1u : 0u,
        text(source.name_), text(source.from_version_), text(source.version_),
        text(source.native_module_), text(source.schema_), dependencies.data(),
        static_cast<uint32_t>(dependencies.size()), scripts.data(), static_cast<uint32_t>(scripts.size()),
        source.native_install_ ? 1u : 0u, prerequisites.data(), static_cast<uint32_t>(prerequisites.size())};
    seekdb_runtime_package *raw = nullptr;
    char diagnostic[512] = {};
    const int32_t status = seekdb_runtime_package_from_source(&input, &raw, diagnostic, sizeof(diagnostic));
    std::unique_ptr<seekdb_runtime_package, decltype(&seekdb_runtime_package_destroy)>
        package(raw, seekdb_runtime_package_destroy);
    if (status != SEEKDB_RUNTIME_OK) {
      error = diagnostic;
      return status == SEEKDB_RUNTIME_NO_MEMORY ? OB_ALLOCATE_MEMORY_FAILED : OB_INVALID_ARGUMENT;
    }
    return package ? OB_SUCCESS : OB_ERR_UNEXPECTED;
  } catch (const std::bad_alloc &) {
    return OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    return OB_ERR_UNEXPECTED;
  }
}

} } }
