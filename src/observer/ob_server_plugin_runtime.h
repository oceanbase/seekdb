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

#ifndef OCEANBASE_OBSERVER_OB_SERVER_PLUGIN_RUNTIME_H_
#define OCEANBASE_OBSERVER_OB_SERVER_PLUGIN_RUNTIME_H_

#include <memory>
#include <string>
#include <vector>
#include "seekdb/plugin/optimizer_spi.h"
#include "seekdb/plugin/server_dev_planner.h"
#include "share/plugin/custom_executor.h"

struct seekdb_plugin_execution_context_v1;
struct seekdb_plugin_batch_context_v1;
struct seekdb_plugin_batch_row_v1;
struct seekdb_plugin_execution_value_v1;
struct seekdb_plugin_sql_binding_v1;
struct seekdb_plugin_sql_cast_binding_v1;
struct seekdb_plugin_sql_column_v1;
struct seekdb_plugin_table_execution_context_v1;
struct seekdb_plugin_table_estimate_v1;
typedef int32_t seekdb_plugin_extension_kind_t;
typedef int32_t seekdb_plugin_cast_context_t;

namespace oceanbase
{
namespace common { class ObISQLClient; class ObString; }
namespace share { class IPluginTableCursor; }
namespace share { namespace plugin {
class IExtensionCatalogInstaller;
class IExtensionCatalogDropper;
class IExtensionCatalogUpdater;
class ICatalogDeclarations;
struct ExtensionPackageSource;
struct ObPluginStatusSnapshot;
} }

namespace observer
{

// ObServer-facing ownership and ready-gate boundary for the optional plugin
// runtime.  Plugin implementation types remain hidden in Impl so an ordinary
// core build neither includes their headers nor references their symbols.
//
// Phase 1 uses a local trusted-directory verifier backed by the durable
// catalog. It deliberately does not perform signature/trust-chain checks;
// the loader still owns path confinement, manifest reconciliation and
// lifecycle recovery.
class ObServerPluginRuntime final
{
public:
  ObServerPluginRuntime();
  ~ObServerPluginRuntime();

  ObServerPluginRuntime(const ObServerPluginRuntime &) = delete;
  ObServerPluginRuntime &operator=(const ObServerPluginRuntime &) = delete;

  int init(common::ObISQLClient *sql_client,
           const std::string &trusted_directory = std::string(),
           const std::string &extension_directory = std::string(),
           uint64_t plugin_memory_limit = UINT64_MAX,
           uint64_t plugin_allocation_limit = UINT64_MAX);
  // Copies immutable startup configuration; no SQL-controlled root/search path.
  // An omitted directory explicitly disables Extension package discovery.
  int extension_package_root(std::string &root) const;
  // Request-scoped copy; caller must hold the normal server request lifetime.
  // No runtime/module pointers or leases escape. Not a catalog/transaction view.
  int list_plugin_status(std::vector<share::plugin::ObPluginStatusSnapshot> &statuses) const;
  int prepare_catalog_install(const share::plugin::ExtensionPackageSource &source, uint64_t tenant_id,
      uint64_t database_id, uint64_t owner_id, std::unique_ptr<share::plugin::ICatalogDeclarations> &output);
  int recover_before_server_ready(std::string &error);
  // MySQL-compatible lifecycle management. Filesystem discovery only finds
  // candidates; these calls persist installation and alter resident runtime.
  int install_plugin(const std::string &plugin_name,
                     const std::string &soname,
                     std::string &error);
  int uninstall_plugin(const std::string &plugin_name, std::string &error);
  // Startup composition only; does not expose the concrete catalog or loader.
  std::shared_ptr<share::plugin::IExtensionCatalogInstaller> extension_catalog_installer() const;
  std::shared_ptr<share::plugin::IExtensionCatalogDropper> extension_catalog_dropper() const;
  std::shared_ptr<share::plugin::IExtensionCatalogUpdater> extension_catalog_updater() const;
  int execute_function(const char *service_id,
                       uint32_t abi_major,
                       uint32_t required_minor,
                       const seekdb_plugin_execution_context_v1 *context,
                       const seekdb_plugin_execution_value_v1 *arguments,
                       uint32_t argument_count);
  int execute_extension(seekdb_plugin_extension_kind_t kind,
                        const char *sql_name,
                        const seekdb_plugin_execution_context_v1 *context,
                        const seekdb_plugin_execution_value_v1 *arguments,
                        uint32_t argument_count);
  int resolve_sql_object(seekdb_plugin_extension_kind_t kind,
                         const char *sql_name,
                         const char *const *argument_type_ids,
                         uint32_t argument_count,
                         seekdb_plugin_sql_binding_v1 *binding);
  int resolve_native_function(const char *module_id, const char *implementation_id,
      const char *const *argument_type_ids, uint32_t argument_count,
      seekdb_plugin_sql_binding_v1 *binding);
  int execute_bound_function(
      const seekdb_plugin_sql_binding_v1 *binding,
      const seekdb_plugin_execution_context_v1 *context,
      const seekdb_plugin_execution_value_v1 *arguments,
      uint32_t argument_count);
  int execute_bound_function_batch(
      const seekdb_plugin_sql_binding_v1 *binding, const seekdb_plugin_batch_context_v1 *context,
      const seekdb_plugin_batch_row_v1 *rows, uint32_t row_count);
  int describe_sql_column(const seekdb_plugin_sql_binding_v1 *binding,
                          uint32_t column_index,
                          seekdb_plugin_sql_column_v1 *column);
  int decode_bound_type(const seekdb_plugin_sql_binding_v1 *binding,
                        const seekdb_plugin_execution_context_v1 *context,
                        const uint8_t *encoded, uint64_t encoded_size);
  int encode_bound_type(const seekdb_plugin_sql_binding_v1 *binding,
                        const seekdb_plugin_execution_context_v1 *context,
                        const seekdb_plugin_execution_value_v1 *value);
  int resolve_common_type(const char *const *type_ids, uint32_t count,
                          std::string &common_type, uint64_t &registry_epoch);
  int resolve_type_by_id(const char *logical_type_id, seekdb_plugin_sql_binding_v1 *binding,
                        uint64_t expected_epoch = 0);
  int check_bound_type_comparison(const seekdb_plugin_sql_binding_v1 &binding);
  int compare_bound_type(const seekdb_plugin_sql_binding_v1 &binding,
      const seekdb_plugin_execution_value_v1 &left, const seekdb_plugin_execution_value_v1 &right,
      int32_t &ordering);
  int resolve_sql_cast(const char *source_type_id, const char *target_type_id,
                       seekdb_plugin_cast_context_t requested_context, seekdb_plugin_sql_cast_binding_v1 *binding,
                       uint64_t expected_epoch = 0);
  int execute_bound_cast(const seekdb_plugin_sql_cast_binding_v1 *binding,
                         const seekdb_plugin_execution_context_v1 *context,
                         const seekdb_plugin_execution_value_v1 *value);
  int open_bound_table_function(
      const seekdb_plugin_sql_binding_v1 *binding,
      const seekdb_plugin_table_execution_context_v1 *context,
      const seekdb_plugin_execution_value_v1 *arguments,
      uint32_t argument_count,
      std::unique_ptr<share::IPluginTableCursor> &cursor);
  int run_optimizer_hooks(const seekdb_plugin_optimizer_info_v1_t &info,
      int (*next)(void *), void *context);
  int run_candidate_hooks(const seekdb_plugin_candidate_context_v1_t &view,
      int (*next)(void *), void *context, int (*validate)(void *),
      seekdb_plugin_candidate_phase_t phase = SEEKDB_PLUGIN_PHASE_SELECT);
  int candidate_hooks_available(seekdb_plugin_candidate_phase_t phase, bool &available);
  int plugin_join_hooks_available(bool &available);
  int bind_custom_executor(const char *service_id, uint32_t major, uint32_t minimum_minor,
      share::plugin::CustomExecutorBinding &binding);
  int open_custom_executor(const share::plugin::CustomExecutorBinding &binding, const uint8_t *plan,
      uint32_t size, std::unique_ptr<share::plugin::ICustomExecutor> &cursor);
  int estimate_bound_table_function(const seekdb_plugin_sql_binding_v1 &binding,
      seekdb_plugin_table_estimate_v1 &estimate);
  int mutate_type_dependency(common::ObISQLClient &sql_client,
                             const seekdb_plugin_sql_binding_v1 &binding,
                             uint64_t table_id,
                             uint64_t column_id,
                             bool add);
  void destroy() noexcept;
  int mutate_routine_dependency(common::ObISQLClient &sql_client,
      const common::ObString &module_id, const common::ObString &implementation_id,
      uint64_t routine_id, bool add, uint64_t expected_generation = 0);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace observer
} // namespace oceanbase

#endif // OCEANBASE_OBSERVER_OB_SERVER_PLUGIN_RUNTIME_H_
