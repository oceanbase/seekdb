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
#include <string>

#include "seekdb/plugin/execution_spi.h"
#include "seekdb/plugin/optimizer_spi.h"
#include "seekdb/plugin/server_dev_planner.h"
#include "share/plugin/custom_executor.h"
#include "lib/ob_errno.h"
#include "seekdb/plugin/extension_spi.h"
#include "seekdb/plugin/sql_catalog.h"

namespace oceanbase
{
namespace common { class ObISQLClient; class ObString; }
namespace share
{
namespace schema { class RoutineCatalogTransaction; }

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
  // Host-only handoff to the live plan cache. Reserve before SQL commit;
  // journal outcome notification controls visibility, not this capability.
  virtual int reserve_routine_invalidations(schema::RoutineCatalogTransaction &, uint64_t)
  { return common::OB_NOT_SUPPORTED; }
  virtual int bind_plugin_custom_executor(const char *, uint32_t, uint32_t,
      plugin::CustomExecutorBinding &binding)
  { binding = {}; return common::OB_NOT_SUPPORTED; }
  virtual int open_plugin_custom_executor(const plugin::CustomExecutorBinding &, const uint8_t *, uint32_t,
      std::unique_ptr<plugin::ICustomExecutor> &)
  { return common::OB_NOT_SUPPORTED; }
  // Default for core-only/controlled providers: no extension is installed.
  virtual int estimate_bound_plugin_table_function(const seekdb_plugin_sql_binding_v1_t &,
      seekdb_plugin_table_estimate_v1_t &estimate)
  { estimate = {}; return common::OB_NOT_SUPPORTED; }
  virtual int run_plugin_optimizer_hooks(const seekdb_plugin_optimizer_info_v1_t &,
      int (*next)(void *), void *context) { return next(context); }
  virtual int run_plugin_candidate_hooks(const seekdb_plugin_candidate_context_v1_t &,
      int (*next)(void *), void *context, int (*validate)(void *))
  { const int ret = next(context); return ret == common::OB_SUCCESS ? validate(context) : ret; }
  virtual int run_plugin_relation_hooks(const seekdb_plugin_candidate_context_v1_t &,
      int (*next)(void *), void *context, int (*validate)(void *))
  { const int ret = next(context); return ret == common::OB_SUCCESS ? validate(context) : ret; }
  virtual int plugin_join_hooks_available(bool &available)
  { available = false; return common::OB_SUCCESS; }
  virtual int plugin_upper_hooks_available(seekdb_plugin_candidate_phase_t phase, bool &available)
  { available = false; return phase >= SEEKDB_PLUGIN_PHASE_GROUP && phase <= SEEKDB_PLUGIN_PHASE_ORDERED ?
      common::OB_SUCCESS : common::OB_INVALID_ARGUMENT; }
  virtual int run_plugin_upper_hooks(seekdb_plugin_candidate_phase_t phase,
      const seekdb_plugin_candidate_context_v1_t &, int (*next)(void *), void *context, int (*validate)(void *))
  {
    if (phase < SEEKDB_PLUGIN_PHASE_GROUP || phase > SEEKDB_PLUGIN_PHASE_ORDERED) return common::OB_INVALID_ARGUMENT;
    const int ret = next(context); return ret == common::OB_SUCCESS ? validate(context) : ret;
  }
  virtual int run_plugin_join_hooks(const seekdb_plugin_candidate_context_v1_t &,
      int (*next)(void *), void *context, int (*validate)(void *))
  { const int ret = next(context); return ret == common::OB_SUCCESS ? validate(context) : ret; }
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
  // Catalog routines bind an exact implementation identity, never a SQL alias.
  // Resolving metadata does not grant EXECUTE or pin a permanent code lease.
  virtual int resolve_plugin_native_function(const char *, const char *,
      const char *const *, uint32_t, seekdb_plugin_sql_binding_v1_t *binding)
  {
    if (binding == nullptr) return common::OB_INVALID_ARGUMENT;
    *binding = {};
    return common::OB_NOT_SUPPORTED;
  }
  virtual int execute_bound_plugin_function(
      const seekdb_plugin_sql_binding_v1_t *binding,
      const seekdb_plugin_execution_context_v1 *context,
      const seekdb_plugin_execution_value_v1 *arguments,
      uint32_t argument_count) = 0;
  virtual int execute_bound_plugin_function_batch(
      const seekdb_plugin_sql_binding_v1_t *, const seekdb_plugin_batch_context_v1_t *,
      const seekdb_plugin_batch_row_v1_t *, uint32_t)
  { return common::OB_NOT_SUPPORTED; }
  virtual int describe_plugin_sql_column(
      const seekdb_plugin_sql_binding_v1_t *binding,
      uint32_t column_index,
      seekdb_plugin_sql_column_v1_t *column) = 0;
  virtual int decode_bound_plugin_type(
      const seekdb_plugin_sql_binding_v1_t *binding,
      const seekdb_plugin_execution_context_v1 *context,
      const uint8_t *encoded, uint64_t encoded_size) = 0;
  virtual int encode_bound_plugin_type(
      const seekdb_plugin_sql_binding_v1_t *binding,
      const seekdb_plugin_execution_context_v1 *context,
      const seekdb_plugin_execution_value_v1 *value) = 0;
  virtual int resolve_plugin_type_by_id(const char *, seekdb_plugin_sql_binding_v1_t *binding,
      uint64_t = 0)
  {
    if (!binding) return common::OB_INVALID_ARGUMENT;
    *binding = {}; return common::OB_NOT_SUPPORTED;
  }
  virtual int check_bound_plugin_type_comparison(const seekdb_plugin_sql_binding_v1_t &)
  { return common::OB_NOT_SUPPORTED; }
  virtual int compare_bound_plugin_type(const seekdb_plugin_sql_binding_v1_t &,
      const seekdb_plugin_execution_value_v1_t &, const seekdb_plugin_execution_value_v1_t &,
      int32_t &ordering)
  { ordering = 0; return common::OB_NOT_SUPPORTED; }
  // Host-internal type composition API, not a public DSO ABI. Inputs are
  // logical IDs, nullptr denotes unknown NULL. Success owns its ID/epoch;
  // failure clears both. Resolving metadata never grants an execution lease.
  virtual int resolve_plugin_common_type(const char *const *type_ids, uint32_t count,
      std::string &common_type, uint64_t &registry_epoch) = 0;
  virtual int resolve_plugin_cast(const char *source_type_id, const char *target_type_id,
      seekdb_plugin_cast_context_t requested_context, seekdb_plugin_sql_cast_binding_v1_t *binding,
      uint64_t expected_epoch = 0) = 0;
  virtual int execute_bound_plugin_cast(const seekdb_plugin_sql_cast_binding_v1_t *binding,
      const seekdb_plugin_execution_context_v1 *context,
      const seekdb_plugin_execution_value_v1 *value) = 0;
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
  // Borrow the caller's schema transaction; never activate code or commit it.
  // Logical identities let DROP clean up an unavailable module after recovery.
  virtual int mutate_native_routine_dependency(common::ObISQLClient &,
      const common::ObString &, const common::ObString &, uint64_t, bool,
      uint64_t expected_generation = 0)
  { return common::OB_NOT_SUPPORTED; }
};

extern ObIModuleProvider *g_mp;

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_RC_OB_MODULE_PROVIDER_H_
