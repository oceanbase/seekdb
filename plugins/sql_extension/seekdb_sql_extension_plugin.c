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

#include "seekdb/plugin/execution_spi.h"
#include "seekdb/plugin/extension_spi.h"

#include <limits.h>
#include <string.h>

struct seekdb_plugin_instance_handle {
  const seekdb_plugin_host_api_v1_t *host_api;
  uint8_t started;
};

struct seekdb_plugin_table_cursor_handle {
  int64_t current;
  int64_t last;
  uint8_t exhausted;
};

static struct seekdb_plugin_instance_handle sql_instance;

static seekdb_plugin_status_t emit_value(
    const seekdb_plugin_execution_context_v1_t *context,
    const char *type_id,
    const uint8_t *data,
    const uint64_t data_size,
    const uint8_t is_null)
{
  if (NULL == context || context->struct_size < sizeof(*context) ||
      NULL == context->emit_result) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result), type_id, data, data_size, is_null,
      {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
  return context->emit_result(context->host, &result);
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL add_one(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance != &sql_instance || !sql_instance.started ||
      argument_count != 1 || NULL == arguments ||
      arguments[0].struct_size < sizeof(arguments[0]) ||
      NULL == arguments[0].type_id ||
      strcmp(arguments[0].type_id, "core.type.int64") != 0 ||
      arguments[0].data_size != sizeof(int64_t) ||
      NULL == arguments[0].data) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  int64_t value = 0;
  memcpy(&value, arguments[0].data, sizeof(value));
  if (value == INT64_MAX) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  ++value;
  return emit_value(context, "core.type.int64",
                    (const uint8_t *)&value, sizeof(value), 0);
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL identity_integer(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance != &sql_instance || !sql_instance.started ||
      argument_count != 1 || NULL == arguments ||
      arguments[0].data_size != sizeof(int64_t)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  return emit_value(context, "core.type.int64", arguments[0].data,
                    arguments[0].data_size, arguments[0].is_null);
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL identity_bytes(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance != &sql_instance || !sql_instance.started ||
      argument_count != 1 || NULL == arguments) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  return emit_value(context, "core.type.bytes", arguments[0].data,
                    arguments[0].data_size, arguments[0].is_null);
}

#define SQL_FUNCTION_SERVICE(callback) \
  {sizeof(seekdb_plugin_function_service_v1_t), \
   SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR, SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, \
   0, callback, {0, 0, 0, 0, 0, 0, 0, 0}}

static const seekdb_plugin_function_service_v1_t add_one_service =
    SQL_FUNCTION_SERVICE(add_one);
static const seekdb_plugin_function_service_v1_t integer_identity_service =
    SQL_FUNCTION_SERVICE(identity_integer);
static const seekdb_plugin_function_service_v1_t bytes_identity_service =
    SQL_FUNCTION_SERVICE(identity_bytes);

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL decode_payload(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const uint8_t *encoded,
    uint64_t encoded_size)
{
  if (instance != &sql_instance || !sql_instance.started ||
      (encoded_size != 0 && NULL == encoded)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  return emit_value(context, "org.seekdb.sql-extension.type.payload",
                    encoded, encoded_size, 0);
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL encode_payload(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *value)
{
  if (instance != &sql_instance || !sql_instance.started || NULL == value ||
      value->struct_size < sizeof(*value) ||
      (value->data_size != 0 && NULL == value->data)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  return emit_value(context, "core.type.bytes", value->data,
                    value->data_size, value->is_null);
}

static const seekdb_plugin_type_codec_service_v1_t payload_codec_service = {
    sizeof(payload_codec_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, decode_payload, encode_payload,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL open_series(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_table_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count,
    seekdb_plugin_table_cursor_handle_t **out_cursor)
{
  if (instance != &sql_instance || !sql_instance.started ||
      NULL == context || NULL == out_cursor || argument_count != 2 ||
      NULL == arguments || NULL == sql_instance.host_api ||
      NULL == sql_instance.host_api->alloc ||
      arguments[0].data_size != sizeof(int64_t) ||
      arguments[1].data_size != sizeof(int64_t) ||
      NULL == arguments[0].data || NULL == arguments[1].data) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  *out_cursor = sql_instance.host_api->alloc(
      sql_instance.host_api->host_handle,
      sizeof(struct seekdb_plugin_table_cursor_handle),
      _Alignof(struct seekdb_plugin_table_cursor_handle));
  if (NULL == *out_cursor) return SEEKDB_PLUGIN_STATUS_NO_MEMORY;
  memcpy(&(*out_cursor)->current, arguments[0].data, sizeof(int64_t));
  memcpy(&(*out_cursor)->last, arguments[1].data, sizeof(int64_t));
  (*out_cursor)->exhausted = 0;
  return SEEKDB_PLUGIN_STATUS_OK;
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL next_series(
    seekdb_plugin_instance_handle_t *instance,
    seekdb_plugin_table_cursor_handle_t *cursor,
    const seekdb_plugin_table_execution_context_v1_t *context,
    uint32_t maximum_rows,
    uint32_t *out_emitted_rows)
{
  if (instance != &sql_instance || !sql_instance.started || NULL == cursor ||
      NULL == context || NULL == context->emit_row ||
      NULL == out_emitted_rows || maximum_rows == 0) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  *out_emitted_rows = 0;
  while (*out_emitted_rows < maximum_rows && !cursor->exhausted &&
         cursor->current <= cursor->last) {
    const int64_t value = cursor->current;
    const seekdb_plugin_execution_result_v1_t column = {
        sizeof(column), "core.type.int64", (const uint8_t *)&value,
        sizeof(value), 0, {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
    const seekdb_plugin_table_row_v1_t row = {
        sizeof(row), &column, 1, 0, {0, 0, 0, 0}};
    const seekdb_plugin_status_t status =
        context->emit_row(context->host, &row);
    if (status != SEEKDB_PLUGIN_STATUS_OK) return status;
    ++(*out_emitted_rows);
    if (cursor->current == INT64_MAX) {
      cursor->exhausted = 1;
      break;
    }
    ++cursor->current;
  }
  return *out_emitted_rows == 0 ? SEEKDB_PLUGIN_STATUS_END_OF_STREAM
                                 : SEEKDB_PLUGIN_STATUS_OK;
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL rescan_series(
    seekdb_plugin_instance_handle_t *instance,
    seekdb_plugin_table_cursor_handle_t *cursor,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance != &sql_instance || NULL == cursor || NULL == arguments ||
      argument_count != 2 || arguments[0].data_size != sizeof(int64_t) ||
      arguments[1].data_size != sizeof(int64_t)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  memcpy(&cursor->current, arguments[0].data, sizeof(int64_t));
  memcpy(&cursor->last, arguments[1].data, sizeof(int64_t));
  cursor->exhausted = 0;
  return SEEKDB_PLUGIN_STATUS_OK;
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL close_series(
    seekdb_plugin_instance_handle_t *instance,
    seekdb_plugin_table_cursor_handle_t *cursor)
{
  if (instance != &sql_instance || NULL == cursor ||
      NULL == sql_instance.host_api || NULL == sql_instance.host_api->free) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  sql_instance.host_api->free(
      sql_instance.host_api->host_handle, cursor, sizeof(*cursor),
      _Alignof(struct seekdb_plugin_table_cursor_handle));
  return SEEKDB_PLUGIN_STATUS_OK;
}

static const seekdb_plugin_table_function_service_v1_t series_service = {
    sizeof(series_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, open_series, next_series,
    rescan_series, close_series, {0, 0, 0, 0, 0, 0, 0, 0}};

#define IMPLEMENTATION(service_name) \
  {sizeof(seekdb_plugin_implementation_ref_v1_t), service_name, \
   {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}}, \
   SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}}

static const seekdb_plugin_type_descriptor_v1_t sql_types[] = {
    {sizeof(seekdb_plugin_type_descriptor_v1_t),
     "org.seekdb.sql-extension.type.payload", "seekdb_payload",
     "org.seekdb.sql-extension.format.payload", 1, 0,
     SEEKDB_PLUGIN_EXTENSION_FLAG_PERSISTENT |
         SEEKDB_PLUGIN_EXTENSION_FLAG_REQUIRES_CATALOG,
     IMPLEMENTATION("org.seekdb.sql-extension.payload-codec"),
     {0, 0, 0, 0}}};

static const char *const integer_signature[] = {"core.type.int64"};
static const char *const bytes_signature[] = {"core.type.bytes"};
static const char *const series_signature[] = {
    "core.type.int64", "core.type.int64"};

#define TYPED_FUNCTION(object_name, function_name, result_type, service_name, signature) \
  {{sizeof(seekdb_plugin_function_descriptor_v2_t), object_name, function_name, \
    1, 1, result_type, \
    SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC | \
        SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE | \
        SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING, \
    IMPLEMENTATION(service_name), {0, 0, 0, 0}}, \
   signature, 1, SEEKDB_PLUGIN_SIGNATURE_FLAG_NONE, {0, 0, 0, 0}}

static const seekdb_plugin_function_descriptor_v2_t sql_functions[] = {
    TYPED_FUNCTION("org.seekdb.sql-extension.function.add-one",
                   "seekdb_add_one", "core.type.int64",
                   "org.seekdb.sql-extension.add-one", integer_signature),
    TYPED_FUNCTION("org.seekdb.sql-extension.function.identity-int",
                   "seekdb_identity", "core.type.int64",
                   "org.seekdb.sql-extension.identity-int", integer_signature),
    TYPED_FUNCTION("org.seekdb.sql-extension.function.identity-bytes",
                   "seekdb_identity", "core.type.bytes",
                   "org.seekdb.sql-extension.identity-bytes", bytes_signature)};

static const seekdb_plugin_table_column_descriptor_v1_t series_columns[] = {
    {sizeof(seekdb_plugin_table_column_descriptor_v1_t), "value",
     "core.type.int64", 0, {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}}};

static const seekdb_plugin_table_function_descriptor_v1_t table_functions[] = {
    {sizeof(seekdb_plugin_table_function_descriptor_v1_t),
     "org.seekdb.sql-extension.table.generate-series", "seekdb_generate_series",
     2, 2, series_signature, 2, SEEKDB_PLUGIN_SIGNATURE_FLAG_NONE,
     series_columns, 1, 0,
     SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
         SEEKDB_PLUGIN_EXTENSION_FLAG_PARALLEL_SAFE,
     IMPLEMENTATION("org.seekdb.sql-extension.generate-series"),
     {0, 0, 0, 0}}};

static const seekdb_plugin_extension_snapshot_v2_t sql_snapshot = {
    {sizeof(seekdb_plugin_extension_snapshot_v2_t),
     sql_types, sizeof(sql_types) / sizeof(sql_types[0]), sizeof(sql_types),
     (const seekdb_plugin_function_descriptor_v1_t *)sql_functions,
     sizeof(sql_functions) / sizeof(sql_functions[0]), sizeof(sql_functions),
     NULL, 0, 0, NULL, 0, 0, NULL, 0, 0, NULL, 0, 0, NULL, 0, 0,
     {0, 0, 0, 0, 0, 0, 0, 0}},
    table_functions, sizeof(table_functions) / sizeof(table_functions[0]),
    sizeof(table_functions), {0, 0, 0, 0}};

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL describe_sql_extensions(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_extension_snapshot_v1_t **snapshot)
{
  if (instance != &sql_instance || !sql_instance.started || NULL == snapshot) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  *snapshot = &sql_snapshot.snapshot;
  return SEEKDB_PLUGIN_STATUS_OK;
}

static const seekdb_plugin_extension_catalog_service_v1_t catalog_service = {
    sizeof(catalog_service), describe_sql_extensions,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL initialize(
    const seekdb_plugin_host_api_v1_t *host,
    seekdb_plugin_instance_handle_t **instance)
{
  if (NULL == host || NULL == instance ||
      host->struct_size < sizeof(*host) ||
      host->abi_major != SEEKDB_PLUGIN_ABI_MAJOR || NULL == host->alloc ||
      NULL == host->free) {
    return SEEKDB_PLUGIN_STATUS_UNSUPPORTED_ABI;
  }
  sql_instance.host_api = host;
  sql_instance.started = 0;
  *instance = &sql_instance;
  return SEEKDB_PLUGIN_STATUS_OK;
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL start(
    seekdb_plugin_instance_handle_t *instance)
{
  if (instance != &sql_instance || sql_instance.started) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  sql_instance.started = 1;
  return SEEKDB_PLUGIN_STATUS_OK;
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL stop(
    seekdb_plugin_instance_handle_t *instance)
{
  if (instance != &sql_instance || !sql_instance.started) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  sql_instance.started = 0;
  return SEEKDB_PLUGIN_STATUS_OK;
}

static void SEEKDB_PLUGIN_CALL deinitialize(
    seekdb_plugin_instance_handle_t *instance)
{
  if (instance == &sql_instance) {
    sql_instance.host_api = NULL;
    sql_instance.started = 0;
  }
}

#define PROVIDED_SERVICE(service_name, table, capability) \
  {sizeof(seekdb_plugin_service_provide_descriptor_t), service_name, \
   {1, 0, 0}, table, capability, {0, 0, 0, 0}}

static const seekdb_plugin_service_provide_descriptor_t provided_services[] = {
    PROVIDED_SERVICE("org.seekdb.sql-extension.add-one", &add_one_service,
                     SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE),
    PROVIDED_SERVICE("org.seekdb.sql-extension.identity-int",
                     &integer_identity_service,
                     SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE),
    PROVIDED_SERVICE("org.seekdb.sql-extension.identity-bytes",
                     &bytes_identity_service,
                     SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE),
    PROVIDED_SERVICE("org.seekdb.sql-extension.generate-series",
                     &series_service, SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE),
    PROVIDED_SERVICE("org.seekdb.sql-extension.payload-codec",
                     &payload_codec_service,
                     SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE),
    PROVIDED_SERVICE("org.seekdb.sql-extension.extensions", &catalog_service,
                     SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE |
                         SEEKDB_PLUGIN_CAPABILITY_EXTENSION_CATALOG)};

static const seekdb_plugin_manifest_v1_t sql_manifest = {
    sizeof(sql_manifest), SEEKDB_PLUGIN_ABI_MAJOR, SEEKDB_PLUGIN_ABI_MINOR,
    "org.seekdb.sql_extension", "seekdb", {1, 0, 0},
    "sql-extension-catalog-v1", 1, 1,
    SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE |
        SEEKDB_PLUGIN_CAPABILITY_PERSISTENT_DATA,
    provided_services, sizeof(provided_services) / sizeof(provided_services[0]),
    NULL, 0, initialize, start, stop, deinitialize,
    {0, 0, 0, 0, 0, 0, 0, 0}};

SEEKDB_PLUGIN_EXPORT const seekdb_plugin_manifest_v1_t *SEEKDB_PLUGIN_CALL
seekdb_plugin_entry_v1(void)
{
  return &sql_manifest;
}
