// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace oceanbase { namespace share { namespace plugin {
struct ExtensionPackageSource;
class ICatalogDeclarations;
struct ObPluginStatusSnapshot;
// Host-owned lifecycle service. SQL borrows this interface for the request;
// only Observer constructs and destroys the concrete runtime.
class IPluginRuntimeService
{
public:
  virtual ~IPluginRuntimeService() = default;
  virtual int extension_package_root(std::string &root) const = 0;
  virtual int list_plugin_status(std::vector<ObPluginStatusSnapshot> &statuses) const = 0;
  virtual int prepare_catalog_install(const ExtensionPackageSource &source,
      uint64_t tenant_id, uint64_t database_id, uint64_t owner_id,
      std::unique_ptr<ICatalogDeclarations> &output) = 0;
  virtual int install_plugin(const std::string &name, const std::string &soname,
      std::string &error) = 0;
  virtual int uninstall_plugin(const std::string &name, std::string &error) = 0;
};
} } }
