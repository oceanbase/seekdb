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

#ifndef SEEKDB_PLUGIN_EXECUTION_SPI_H_
#define SEEKDB_PLUGIN_EXECUTION_SPI_H_

#include "seekdb/plugin/seekdb_plugin_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Executable plugin values are deliberately byte-oriented.  ObDatum,
 * transaction, plan and tablet objects never cross this boundary.  The host
 * owns all input buffers for the duration of execute(); output bytes are
 * copied into a host-owned result sink before the call returns.
 */
#define SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR 1u
#define SEEKDB_PLUGIN_EXECUTION_SPI_MINOR 0u
#define SEEKDB_PLUGIN_MAX_ARGUMENTS 1024u

typedef struct seekdb_plugin_execution_value_v1 {
  uint32_t struct_size;
  const char *type_id;
  const uint8_t *data;
  uint64_t data_size;
  uint8_t is_null;
  uint8_t reserved_bytes[7];
  uint64_t reserved[4];
} seekdb_plugin_execution_value_v1_t;

typedef struct seekdb_plugin_execution_result_v1 {
  uint32_t struct_size;
  const char *type_id;
  const uint8_t *data;
  uint64_t data_size;
  uint8_t is_null;
  uint8_t reserved_bytes[7];
  uint64_t reserved[4];
} seekdb_plugin_execution_result_v1_t;

typedef seekdb_plugin_status_t(SEEKDB_PLUGIN_CALL *
    seekdb_plugin_emit_result_v1_fn)(
    seekdb_plugin_host_handle_t *host,
    const seekdb_plugin_execution_result_v1_t *result);

typedef struct seekdb_plugin_execution_context_v1 {
  uint32_t struct_size;
  seekdb_plugin_host_handle_t *host;
  seekdb_plugin_emit_result_v1_fn emit_result;
  uint64_t reserved[6];
} seekdb_plugin_execution_context_v1_t;

typedef seekdb_plugin_status_t(SEEKDB_PLUGIN_CALL *
    seekdb_plugin_function_execute_v1_fn)(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count);

typedef struct seekdb_plugin_function_service_v1 {
  uint32_t struct_size;
  uint32_t spi_major;
  uint32_t spi_minor;
  uint32_t reserved_word;
  seekdb_plugin_function_execute_v1_fn execute;
  uint64_t reserved[8];
} seekdb_plugin_function_service_v1_t;

typedef seekdb_plugin_status_t(SEEKDB_PLUGIN_CALL *
    seekdb_plugin_type_decode_v1_fn)(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const uint8_t *encoded,
    uint64_t encoded_size);

typedef seekdb_plugin_status_t(SEEKDB_PLUGIN_CALL *
    seekdb_plugin_type_encode_v1_fn)(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *value);

typedef struct seekdb_plugin_type_codec_service_v1 {
  uint32_t struct_size;
  uint32_t spi_major;
  uint32_t spi_minor;
  uint32_t reserved_word;
  seekdb_plugin_type_decode_v1_fn decode;
  seekdb_plugin_type_encode_v1_fn encode;
  uint64_t reserved[8];
} seekdb_plugin_type_codec_service_v1_t;

typedef struct seekdb_plugin_table_cursor_handle
    seekdb_plugin_table_cursor_handle_t;

typedef struct seekdb_plugin_table_row_v1 {
  uint32_t struct_size;
  const seekdb_plugin_execution_result_v1_t *columns;
  uint32_t column_count;
  uint32_t reserved_word;
  uint64_t reserved[4];
} seekdb_plugin_table_row_v1_t;

typedef seekdb_plugin_status_t(SEEKDB_PLUGIN_CALL *
    seekdb_plugin_emit_row_v1_fn)(
    seekdb_plugin_host_handle_t *host,
    const seekdb_plugin_table_row_v1_t *row);

typedef struct seekdb_plugin_table_execution_context_v1 {
  uint32_t struct_size;
  seekdb_plugin_host_handle_t *host;
  seekdb_plugin_emit_row_v1_fn emit_row;
  uint64_t reserved[6];
} seekdb_plugin_table_execution_context_v1_t;

typedef seekdb_plugin_status_t(SEEKDB_PLUGIN_CALL *
    seekdb_plugin_table_open_v1_fn)(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_table_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count,
    seekdb_plugin_table_cursor_handle_t **out_cursor);

typedef seekdb_plugin_status_t(SEEKDB_PLUGIN_CALL *
    seekdb_plugin_table_next_v1_fn)(
    seekdb_plugin_instance_handle_t *instance,
    seekdb_plugin_table_cursor_handle_t *cursor,
    const seekdb_plugin_table_execution_context_v1_t *context,
    uint32_t maximum_rows,
    uint32_t *out_emitted_rows);

typedef seekdb_plugin_status_t(SEEKDB_PLUGIN_CALL *
    seekdb_plugin_table_rescan_v1_fn)(
    seekdb_plugin_instance_handle_t *instance,
    seekdb_plugin_table_cursor_handle_t *cursor,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count);

typedef seekdb_plugin_status_t(SEEKDB_PLUGIN_CALL *
    seekdb_plugin_table_close_v1_fn)(
    seekdb_plugin_instance_handle_t *instance,
    seekdb_plugin_table_cursor_handle_t *cursor);

typedef struct seekdb_plugin_table_function_service_v1 {
  uint32_t struct_size;
  uint32_t spi_major;
  uint32_t spi_minor;
  uint32_t reserved_word;
  seekdb_plugin_table_open_v1_fn open;
  seekdb_plugin_table_next_v1_fn next;
  seekdb_plugin_table_rescan_v1_fn rescan;
  seekdb_plugin_table_close_v1_fn close;
  uint64_t reserved[8];
} seekdb_plugin_table_function_service_v1_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SEEKDB_PLUGIN_EXECUTION_SPI_H_ */
