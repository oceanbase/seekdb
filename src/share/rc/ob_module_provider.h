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

#ifndef OCEANBASE_SHARE_RC_OB_MODULE_PROVIDER_H_
#define OCEANBASE_SHARE_RC_OB_MODULE_PROVIDER_H_

#include "seekdb/plugin/execution_spi.h"
#include "seekdb/plugin/extension_spi.h"

namespace oceanbase
{
namespace share
{

// Compatibility bridge for SQL expression adapters.  The broader module
// provider was replaced by typed server service slots during the master merge;
// plugin execution remains a narrow, versioned boundary.
class ObIModuleProvider
{
public:
  virtual ~ObIModuleProvider() = default;
  virtual int execute_plugin_function(
      const char *service_id,
      uint32_t abi_major,
      uint32_t required_minor,
      const seekdb_plugin_execution_context_v1 *context,
      const seekdb_plugin_execution_value_v1 *arguments,
      uint32_t argument_count) = 0;
  virtual int execute_plugin_extension(
      seekdb_plugin_extension_kind_t kind,
      const char *sql_name,
      const seekdb_plugin_execution_context_v1 *context,
      const seekdb_plugin_execution_value_v1 *arguments,
      uint32_t argument_count) = 0;
};

extern ObIModuleProvider *g_mp;

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_RC_OB_MODULE_PROVIDER_H_
