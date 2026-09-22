/* Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0 */
#ifndef OCEANBASE_SQL_PLUGIN_SQL_CONTEXT_H_
#define OCEANBASE_SQL_PLUGIN_SQL_CONTEXT_H_

#include <thread>
#include "seekdb/plugin/sql_spi.h"

namespace oceanbase { namespace sql {
class ObExecContext;

// One instance per synchronous plugin callback, never stored in a plan/cursor.
class PluginSqlContext
{
public:
  explicit PluginSqlContext(ObExecContext &context);
  void attach(seekdb_plugin_execution_context_v2_t &context);
  void attach(seekdb_plugin_table_execution_context_v2_t &context);
  void attach(seekdb_plugin_table_execution_context_v3_t &context);
  int error() const { return error_; }

private:
  static const seekdb_plugin_sql_api_v1_t *sql_api();
  static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL mutate_routine(
      seekdb_plugin_sql_context_handle_t *, const char *sql, uint64_t sql_size,
      seekdb_plugin_routine_mutation_result_v1_t *);
  static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL lookup_routine(
      seekdb_plugin_sql_context_handle_t *, uint32_t kind, const char *name, uint64_t name_size,
      seekdb_plugin_routine_lookup_result_v1_t *);
  static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL poll_query(
      seekdb_plugin_sql_context_handle_t *context, seekdb_plugin_query_status_v1_t *status);
  static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL execute(
      seekdb_plugin_sql_context_handle_t *context,
      const char *sql, uint64_t sql_size,
      const seekdb_plugin_sql_value_v1_t *parameters, uint32_t parameter_count,
      uint64_t max_rows, seekdb_plugin_sql_consume_row_v1_fn consume_row,
      void *consumer, seekdb_plugin_sql_result_v1_t *result);
  ObExecContext &context_;
  const std::thread::id thread_;
  bool executing_;
  int error_;
};
} }
#endif
