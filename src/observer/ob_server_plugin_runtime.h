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

struct seekdb_plugin_execution_context_v1;
struct seekdb_plugin_execution_value_v1;
typedef int32_t seekdb_plugin_extension_kind_t;

namespace oceanbase
{
namespace share
{
class ObSQLiteConnectionPool;
}

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

  int init(share::ObSQLiteConnectionPool *meta_db_pool,
           const std::string &trusted_directory = std::string());
  int recover_before_server_ready(std::string &error);
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
  void destroy() noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace observer
} // namespace oceanbase

#endif // OCEANBASE_OBSERVER_OB_SERVER_PLUGIN_RUNTIME_H_
