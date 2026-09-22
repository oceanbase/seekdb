/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_PLUGIN_SERVER_DEV_EXECUTOR_H_
#define SEEKDB_PLUGIN_SERVER_DEV_EXECUTOR_H_
#include "seekdb/plugin/execution_spi.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Version-bound custom executor, not a long-lived Public SPI. A service owns
 * its algorithm/state; the host owns child operators, row transport and SQL
 * schema checking. No C++ plan/datum layout crosses this interface.
 * Calls on one cursor are exclusive and synchronous, but may move threads.
 * Different cursors (including open/close) may run concurrently on the same
 * instance. The service must declare THREAD_SAFE; a manifest-level flag alone
 * is insufficient. Plugin-owned shared state must be synchronized. Lifecycle
 * stop/deinit do not overlap live cursors, whose leases delay terminal stop.
 * Input rows are borrowed until the next host callback or end of next().
 * emit copies synchronously; outputs are provisional until next returns OK.
 * Native code remains trusted. No arbitrary grammar/storage/PX support is
 * implied by registering this service. */
#define SEEKDB_PLUGIN_CUSTOM_MAX_INPUTS 64u
#define SEEKDB_PLUGIN_CUSTOM_MAX_COLUMNS 1024u
#define SEEKDB_PLUGIN_CUSTOM_MAX_ROW_BYTES (16u * 1024u * 1024u)
#define SEEKDB_PLUGIN_CUSTOM_MAX_PLAN_BYTES (64u * 1024u)
typedef struct seekdb_plugin_custom_row_v1 {
  uint32_t struct_size;
  uint32_t column_count;
  const seekdb_plugin_execution_value_v1_t *values;
  uint64_t reserved[4];
} seekdb_plugin_custom_row_v1_t;

/* Every callback initializes database_error. Nonzero errors are sticky even
 * if the plugin ignores them. END_OF_STREAM is legal only for next_input.
 * The host must check input schema and copy/validate output before returning.
 * check_interrupt is called by the adapter at entry and after plugin next;
 * long-running algorithms must also call it cooperatively. */
typedef struct seekdb_plugin_custom_context_v1 {
  uint32_t struct_size;
  uint32_t input_count;
  uint32_t output_column_count;
  uint32_t reserved_word;
  void *host_context;
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *next_input)(void *, uint32_t,
      seekdb_plugin_custom_row_v1_t *, int32_t *database_error);
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *emit)(void *,
      const seekdb_plugin_execution_value_v1_t *, uint32_t, int32_t *database_error);
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *check_interrupt)(void *, int32_t *database_error);
  uint64_t reserved[4];
} seekdb_plugin_custom_context_v1_t;

/* Optional, append-only context suffix. Schema is available before any input
 * row (including empty inputs) and immutable for the whole next callback.
 * All arrays are host-owned and callback-borrowed; copy metadata to retain it.
 * Input schemas are indexed by v1.input_count; output is a separate relation.
 * Zero columns is a valid schema, not an unknown/unavailable schema.
 * sql_type/collation are identifiers from the matched server build. precision
 * and scale are SQL numeric metadata (-1 means unknown/not applicable), not
 * memory-layout instructions. Plugins exchange encoding, never SQL locators.
 * This supplies execution schemas, not planner expression identities or a
 * promise that the current SQL adapter supports multi-input/custom outputs. */
#define SEEKDB_PLUGIN_CUSTOM_COLUMN_NULLABLE 1u
#define SEEKDB_PLUGIN_CUSTOM_COLUMN_STORED 2u
#define SEEKDB_PLUGIN_CUSTOM_ENCODING_BYTES 0u
#define SEEKDB_PLUGIN_CUSTOM_ENCODING_NULL 1u
#define SEEKDB_PLUGIN_CUSTOM_ENCODING_BOOL 2u
#define SEEKDB_PLUGIN_CUSTOM_ENCODING_INT32 3u
#define SEEKDB_PLUGIN_CUSTOM_ENCODING_UINT32 4u
#define SEEKDB_PLUGIN_CUSTOM_ENCODING_INT64 5u
#define SEEKDB_PLUGIN_CUSTOM_ENCODING_UINT64 6u
#define SEEKDB_PLUGIN_CUSTOM_ENCODING_FLOAT64 7u
typedef struct seekdb_plugin_custom_column_v1 {
  uint32_t struct_size;
  uint32_t flags;
  uint32_t encoding;
  uint32_t sql_type;
  int32_t collation;
  int32_t precision;
  int32_t scale;
  uint32_t reserved_word;
  char type_id[SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES + 1];
  uint64_t reserved[4];
} seekdb_plugin_custom_column_v1_t;
typedef struct seekdb_plugin_custom_schema_v1 {
  uint32_t struct_size;
  uint32_t column_count;
  const seekdb_plugin_custom_column_v1_t *columns;
  uint64_t reserved[4];
} seekdb_plugin_custom_schema_v1_t;
typedef struct seekdb_plugin_custom_context_v2 {
  seekdb_plugin_custom_context_v1_t v1;
  const seekdb_plugin_custom_schema_v1_t *inputs;
  const seekdb_plugin_custom_schema_v1_t *output;
  uint64_t reserved[4];
} seekdb_plugin_custom_context_v2_t;

/* Optional input-control suffix, understood by executor service minor=1.
 * rescan_input rewinds exactly one child in its CURRENT parameter environment;
 * it neither binds parameters nor resets sibling inputs or plugin-owned state.
 * It is valid before reading, at EOF, and repeatedly, but only before emit in
 * the current next callback. Every borrowed input row becomes invalid.
 * Success does not promise identical rows: repeatability/volatility and the
 * number/timing of child executions are the plugin algorithm's responsibility.
 * Errors (including cancellation) poison the cursor just like next_input;
 * END_OF_STREAM is never a valid rescan result. Recovery requires a successful
 * whole-operator rescan. The host polls cancellation before and after rewinding.
 * Old service minor=0 receives only v1/v2. A minor=1 service may still receive
 * an older host context and must check capability before using this callback.
 * Parameter binding/ownership is a separate protocol, not implied here. */
typedef struct seekdb_plugin_custom_context_v3 {
  seekdb_plugin_custom_context_v2_t v2;
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *rescan_input)(void *, uint32_t,
      int32_t *database_error);
  uint64_t reserved[4];
} seekdb_plugin_custom_context_v3_t;
/* Optional binding-control suffix, executor service minor=2. Bind the declared
 * owned parameters from the most recent successful source-input row, then
 * rescan the target. The host owns immutable snapshots, not plugin buffers.
 * Advancing or independently rewinding the source invalidates the snapshot,
 * owned parameters and target. After a rewind, a successful source read and
 * bind are required before target access; a rewind alone is not a rebind.
 * Repeated source rewinds and rewinds at EOF are legal. Rebinding
 * the same source row is legal. No source row, unbound target, wrong input,
 * partial failure, or emit-before-bind can be treated as success. Cancellation
 * is checked before/after; a failure requires whole-operator rescan to recover.
 * This does not write arbitrary prepared/user parameters or change schemas.
 * Older services receive their v1/v2/v3 prefix without this suffix. */
typedef struct seekdb_plugin_custom_context_v4 {
  seekdb_plugin_custom_context_v3_t v3;
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *bind_rescan_input)(void *, uint32_t,
      int32_t *database_error);
  uint64_t reserved[4];
} seekdb_plugin_custom_context_v4_t;

/* open must copy plan bytes before returning, and set *cursor=NULL on failure.
 * Host closes any nonnull cursor, including one returned alongside failure.
 * next: OK requires exactly one emit; EOF requires none. Other results poison
 * the cursor until successful rescan. Host resets children before rescan;
 * plugin resets only its owned state, never a borrowed query context.
 * close consumes the cursor on every return, including errors. The module
 * lease remains pinned until close returns. No callback may retain context. */
typedef struct seekdb_plugin_custom_executor_v1 {
  uint32_t struct_size;
  uint32_t spi_major;
  uint32_t spi_minor;
  uint32_t reserved_word;
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *open)(seekdb_plugin_instance_handle_t *,
      const uint8_t *, uint32_t, void **cursor);
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *next)(seekdb_plugin_instance_handle_t *,
      void *cursor, const seekdb_plugin_custom_context_v1_t *);
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *rescan)(seekdb_plugin_instance_handle_t *, void *cursor);
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *close)(seekdb_plugin_instance_handle_t *, void *cursor);
  uint64_t reserved[4];
} seekdb_plugin_custom_executor_v1_t;
#ifdef __cplusplus
}
#endif
#endif
