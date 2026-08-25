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

#include <memory>

#include "seekdb/plugin/execution_spi.h"
#include "seekdb/plugin/extension_spi.h"
#include "seekdb/plugin/sql_catalog.h"

namespace oceanbase
{
namespace common { class ObISQLClient; }
namespace share
{

// A live table iterator owns the extension and implementation leases until
// close(), so logical plugin disable cannot overtake an executing scan.
class IPluginTableCursor
{
public:
  virtual ~IPluginTableCursor() = default;
  virtual int next(const seekdb_plugin_table_execution_context_v1_t *context,
                   uint32_t maximum_rows,
                   uint32_t *emitted_rows) = 0;
  virtual int rescan(const seekdb_plugin_execution_value_v1_t *arguments,
                     uint32_t argument_count) = 0;
  virtual int close() = 0;
};

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
  virtual int resolve_plugin_sql_object(
      seekdb_plugin_extension_kind_t kind,
      const char *sql_name,
      const char *const *argument_type_ids,
      uint32_t argument_count,
      seekdb_plugin_sql_binding_v1_t *binding) = 0;
  virtual int execute_bound_plugin_function(
      const seekdb_plugin_sql_binding_v1_t *binding,
      const seekdb_plugin_execution_context_v1 *context,
      const seekdb_plugin_execution_value_v1 *arguments,
      uint32_t argument_count) = 0;
  virtual int describe_plugin_sql_column(
      const seekdb_plugin_sql_binding_v1_t *binding,
      uint32_t column_index,
      seekdb_plugin_sql_column_v1_t *column) = 0;
  virtual int open_bound_plugin_table_function(
      const seekdb_plugin_sql_binding_v1_t *binding,
      const seekdb_plugin_table_execution_context_v1_t *context,
      const seekdb_plugin_execution_value_v1_t *arguments,
      uint32_t argument_count,
      std::unique_ptr<IPluginTableCursor> &cursor) = 0;
  virtual int mutate_plugin_type_dependency(
      common::ObISQLClient &sql_client,
      const seekdb_plugin_sql_binding_v1_t &binding,
      uint64_t table_id,
      uint64_t column_id,
      bool add) = 0;
};

extern ObIModuleProvider *g_mp;

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_RC_OB_MODULE_PROVIDER_H_
