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
 *
 * Builtin non-NULL numbers use native-endian bytes (this is an in-process
 * ABI, not a portable persisted/wire format): core.type.bool is one byte
 * (0 or 1); core.type.int32/uint32 are 4 bytes; core.type.int64/uint64 and
 * core.type.float64 are 8 bytes (float64 is a C double). Buffers need not be
 * naturally aligned: copy bytes rather than dereferencing a typed pointer.
 * SQL narrow integer/float types may be promoted to int64/uint64/float64;
 * the logical type_id determines the actual representation. User-defined
 * type IDs do not inherit a builtin encoding merely by sharing its suffix.
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

/* Optional scalar service suffix. Do not change the baseline minor required by
 * v1 services. Minor 1 opts into sql_spi.h's execution context; minor 2 also
 * supplies metadata-only result resolution. Existing v1 prefixes stay intact. */
#define SEEKDB_PLUGIN_EXECUTION_RESULT_TYPE_MINOR 2u

typedef struct seekdb_plugin_resolved_type_v1 {
  uint32_t struct_size;
  char type_id[SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES + 1];
  uint64_t reserved[4];
} seekdb_plugin_resolved_type_v1_t;

/* Called after overload selection, using effective argument types AFTER the
 * selected signature's implicit coercions (not argument values). An untyped
 * arity envelope receives the original types; NULL denotes an unknown NULL.
 *
 * Must be deterministic for these types and this module generation, thread
 * safe, synchronous, and side-effect free. No SQL/session/transaction context
 * is supplied. May run repeatedly during planning; must not retain pointers.
 * Host initializes out_type to zero with struct_size set. On OK, return one
 * nonempty, bounded NUL-terminated logical ID without altering size/reserved.
 * A plugin unable to determine a type must return an error, not an empty ID.
 * No exception/panic may cross this boundary. */
typedef seekdb_plugin_status_t(SEEKDB_PLUGIN_CALL *
    seekdb_plugin_function_resolve_result_v1_fn)(
    seekdb_plugin_instance_handle_t *instance,
    const char *const *argument_type_ids,
    uint32_t argument_count,
    seekdb_plugin_resolved_type_v1_t *out_type);

typedef struct seekdb_plugin_function_service_v2 {
  seekdb_plugin_function_service_v1_t v1;
  seekdb_plugin_function_resolve_result_v1_fn resolve_result;
  uint64_t resolution_reserved[4];
} seekdb_plugin_function_service_v2_t;

/* Optional scalar batch suffix. A single invocation owns the complete batch;
 * this is not the host repeatedly calling execute(). Old execute/resolve_result
 * prefixes are unchanged. A fixed-result service may omit resolve_result.
 *
 * Rows are compacted by the host: no skipped/evaluated rows are included. Each
 * row has the selected function's effective argument types after coercion and
 * the same arity. All descriptors, identifiers and payloads are borrowed for
 * this synchronous call, on the caller's query thread. No foreign unwind.
 * Limits count payload bytes per cell even when buffers alias; they are not
 * an accounting limit for plugin-owned model/state allocations.
 *
 * On OK exactly one result must have been emitted for every row index in
 * [0,row_count), in any order. The host copies each result synchronously and
 * validates its bound logical type. Duplicate/missing/out-of-range results,
 * errors (including END_OF_STREAM), or cancellation fail the entire batch.
 * Partial output must not be consumed; external side effects are NOT undone.
 * A plugin must not retry an emit that returned an error.
 *
 * query_context lends the existing SQL/query-control services, if advertised
 * by its size. Its scalar emit_result is not a batch output channel; use the
 * indexed batch emitter. Session/transaction/thread lifetime rules are the
 * same as for a scalar call. This is neither an async nor a columnar ABI. */
#define SEEKDB_PLUGIN_EXECUTION_BATCH_MINOR 3u
#define SEEKDB_PLUGIN_MAX_BATCH_ROWS 1024u
#define SEEKDB_PLUGIN_MAX_BATCH_BYTES (UINT64_C(64) * 1024u * 1024u)
typedef struct seekdb_plugin_batch_row_v1 {
  uint32_t struct_size;
  uint32_t argument_count;
  const seekdb_plugin_execution_value_v1_t *arguments;
  uint64_t reserved[4];
} seekdb_plugin_batch_row_v1_t;
typedef seekdb_plugin_status_t(SEEKDB_PLUGIN_CALL *seekdb_plugin_emit_batch_result_v1_fn)(
    seekdb_plugin_host_handle_t *host, uint32_t row_index,
    const seekdb_plugin_execution_result_v1_t *result);
typedef struct seekdb_plugin_batch_context_v1 {
  uint32_t struct_size;
  uint32_t reserved_word;
  const seekdb_plugin_execution_context_v1_t *query_context;
  seekdb_plugin_host_handle_t *host;
  seekdb_plugin_emit_batch_result_v1_fn emit_result;
  uint64_t reserved[4];
} seekdb_plugin_batch_context_v1_t;
typedef seekdb_plugin_status_t(SEEKDB_PLUGIN_CALL *seekdb_plugin_function_execute_batch_v1_fn)(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_batch_context_v1_t *context,
    const seekdb_plugin_batch_row_v1_t *rows, uint32_t row_count);
typedef struct seekdb_plugin_function_service_v3 {
  seekdb_plugin_function_service_v2_t v2;
  seekdb_plugin_function_execute_batch_v1_fn execute_batch;
  uint64_t batch_reserved[4];
} seekdb_plugin_function_service_v3_t;

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

/* Optional, generation-stable comparison semantics for a logical type. This
 * suffix is independent of scalar/table service minors. Existing codecs need
 * not implement ordering. No SQL, allocator, session or continuation is lent.
 * Both values are decoded, non-NULL instances of the bound logical type.
 * Inputs are borrowed only for this synchronous call (at most 16 MiB each).
 *
 * A comparator opts into a deterministic total ordering: antisymmetric and
 * transitive, with zero iff values are equal under that ordering. It must not
 * depend on session collation, mutable state, addresses or invocation order.
 * Physical encoding need not have the same ordering. NULL ordering belongs to
 * the SQL consumer, not this callback. No panic/exception may cross the ABI.
 * Hashing, operator families and index semantics require separate contracts;
 * this suffix alone does not enable host sorting, grouping or index scans. */
#define SEEKDB_PLUGIN_TYPE_COMPARISON_MINOR 1u
typedef struct seekdb_plugin_type_comparison_v1 {
  uint32_t struct_size;
  int32_t ordering; /* Exactly -1, 0 or 1 on success; zero on failure. */
  uint64_t reserved[4];
} seekdb_plugin_type_comparison_v1_t;
typedef seekdb_plugin_status_t(SEEKDB_PLUGIN_CALL *seekdb_plugin_type_compare_v1_fn)(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_value_v1_t *left,
    const seekdb_plugin_execution_value_v1_t *right,
    seekdb_plugin_type_comparison_v1_t *result);
typedef struct seekdb_plugin_type_codec_service_v2 {
  seekdb_plugin_type_codec_service_v1_t v1;
  seekdb_plugin_type_compare_v1_fn compare;
  uint64_t comparison_reserved[4];
} seekdb_plugin_type_codec_service_v2_t;

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

/* Optional metadata-only planning support. This does not execute arguments,
 * open a cursor or grant SQL/session access. All strings are borrowed for this
 * synchronous call. Argument types describe the declared signature, after
 * implicit coercion, not runtime values or guaranteed constant expressions. */
#define SEEKDB_PLUGIN_TABLE_PLANNING_MINOR 1u
#define SEEKDB_PLUGIN_TABLE_QUERY_CONTROL_MINOR 2u
typedef struct seekdb_plugin_table_planning_info_v1 {
  uint32_t struct_size;
  uint32_t argument_count;
  const char *object_id;
  const char *const *argument_type_ids;
  uint32_t column_count;
  uint32_t reserved_word;
  uint64_t reserved[4];
} seekdb_plugin_table_planning_info_v1_t;
typedef struct seekdb_plugin_table_estimate_v1 {
  uint32_t struct_size;
  uint32_t reserved_word;
  double rows;
  double row_width;
  double total_cost;
  uint64_t reserved[4];
} seekdb_plugin_table_estimate_v1_t;
/* Estimates are finite and nonnegative; total_cost uses the host optimizer's
 * relative cost units (not wall-clock milliseconds), row_width is bytes. They
 * guide planning only and never limit result cardinality or cursor execution.
 * Fill the caller-owned result on success; failures are planning errors. */
typedef seekdb_plugin_status_t(SEEKDB_PLUGIN_CALL *seekdb_plugin_table_estimate_v1_fn)(
    seekdb_plugin_instance_handle_t *, const seekdb_plugin_table_planning_info_v1_t *,
    seekdb_plugin_table_estimate_v1_t *);
/* Opt in with v1.spi_minor >= TABLE_PLANNING_MINOR and v1.struct_size >= sizeof
 * this entire structure. Existing minor-0 services retain host default costs;
 * v1 is unchanged. No execution-context extension is implied by this minor. */
typedef struct seekdb_plugin_table_function_service_v2 {
  seekdb_plugin_table_function_service_v1_t v1;
  seekdb_plugin_table_estimate_v1_fn estimate;
  uint64_t reserved[4];
} seekdb_plugin_table_function_service_v2_t;
/* Minor 2 uses this same complete service layout and requests sql_spi.h's
 * table execution context v2. estimate may be NULL in minor 2, selecting host
 * defaults; minor 1 continues to require it. Older hosts may reject a missing
 * estimate. New plugins must handle a host supplying only context v1.
 */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SEEKDB_PLUGIN_EXECUTION_SPI_H_ */
