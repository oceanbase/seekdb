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

#ifndef SEEKDB_PLUGIN_SQL_SPI_H_
#define SEEKDB_PLUGIN_SQL_SPI_H_

#include "seekdb/plugin/execution_spi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SEEKDB_PLUGIN_SQL_SPI_MAJOR 1u
#define SEEKDB_PLUGIN_SQL_SPI_MINOR 0u
/* Scalar service table spi_minor opting into the extended execution context.
 * This is independent of the service's business version and SQL API version. */
#define SEEKDB_PLUGIN_EXECUTION_SQL_CONTEXT_MINOR 1u

typedef struct seekdb_plugin_sql_context_handle seekdb_plugin_sql_context_handle_t;

/* Values are borrowed for the duration of execute/consume_row only. Text is
 * UTF-8; bytes are binary. Numeric values use native C representation. */
typedef enum seekdb_plugin_sql_value_kind {
  SEEKDB_PLUGIN_SQL_NULL = 0,
  SEEKDB_PLUGIN_SQL_INT64 = 1,
  SEEKDB_PLUGIN_SQL_UINT64 = 2,
  SEEKDB_PLUGIN_SQL_FLOAT64 = 3,
  SEEKDB_PLUGIN_SQL_TEXT = 4,
  SEEKDB_PLUGIN_SQL_BYTES = 5
} seekdb_plugin_sql_value_kind_t;

typedef struct seekdb_plugin_sql_value_v1 {
  uint32_t struct_size;
  uint32_t kind;
  const void *data;
  uint64_t data_size;
  uint64_t reserved[2];
} seekdb_plugin_sql_value_v1_t;

typedef seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *seekdb_plugin_sql_consume_row_v1_fn)(
    void *consumer, const seekdb_plugin_sql_value_v1_t *columns, uint32_t column_count);

typedef struct seekdb_plugin_sql_result_v1 {
  uint32_t struct_size;
  uint32_t reserved_word;
  int64_t database_error;
  int64_t affected_rows;
  uint64_t returned_rows;
  uint64_t reserved[2];
} seekdb_plugin_sql_result_v1_t;

/* Synchronous, positional '?' parameters; never string interpolation.
 * Accepts SELECT/INSERT/UPDATE/DELETE/REPLACE. DDL and transaction control are
 * rejected after prepare and before execution. SQL runs as the caller and
 * uses its transaction context. The host establishes statement savepoints on
 * enclosing SELECTs before execution; they survive individual plugin returns
 * and are finalized when the containing result sets close. No internal commit.
 * Any SQL/consumer failure fails the outer
 * plugin invocation even if the plugin ignores the returned status.
 *
 * Contexts cannot be retained, used on another thread, or recursively entered
 * from consume_row. All rows must be consumed before returning. max_rows is
 * an error limit, not SQL LIMIT: exceeding it fails, never silently truncates.
 * Values not representable by the kinds above require an explicit SQL CAST.
 * SQL and parameter bytes, and total delivered result bytes, each have a
 * 16 MiB host limit. Parameters/columns are limited to 1024; nesting to 64.
 * A SELECT can call existing SQL functions; this is not a side-effect sandbox.
 * No init/start, background-session or asynchronous access in this revision.
 */
typedef struct seekdb_plugin_sql_api_v1 {
  uint32_t struct_size;
  uint32_t spi_major;
  uint32_t spi_minor;
  uint32_t reserved_word;
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *execute)(
      seekdb_plugin_sql_context_handle_t *context,
      const char *sql, uint64_t sql_size,
      const seekdb_plugin_sql_value_v1_t *parameters, uint32_t parameter_count,
      uint64_t max_rows, seekdb_plugin_sql_consume_row_v1_fn consume_row,
      void *consumer, seekdb_plugin_sql_result_v1_t *result);
  uint64_t reserved[6];
} seekdb_plugin_sql_api_v1_t;

/* Optional SQL API minor 1 suffix, usable without executing SQL. Cooperative
 * checks of caller cancellation, deadline and host execution status. Same
 * thread and lifetime as execute; no retained token, thread interruption or
 * asynchronous cancellation guarantee. Plugins doing long work should poll
 * between bounded chunks and propagate failures. External effects cannot be
 * undone by cancellation. Errors share execute's sticky invocation state.
 * remaining_us = -1 means no configured query deadline; otherwise a snapshot
 * >=0 (not a reservation). On failure database_error preserves the host error
 * and remaining_us is zero. Inspect API size/major/minor before reading poll.
 */
#define SEEKDB_PLUGIN_SQL_QUERY_CONTROL_MINOR 1u
typedef struct seekdb_plugin_query_status_v1 {
  uint32_t struct_size;
  uint32_t reserved_word;
  int64_t database_error;
  int64_t remaining_us;
  uint64_t reserved[4];
} seekdb_plugin_query_status_v1_t;
typedef struct seekdb_plugin_sql_api_v2 {
  seekdb_plugin_sql_api_v1_t v1;
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *poll_query)(
      seekdb_plugin_sql_context_handle_t *, seekdb_plugin_query_status_v1_t *);
  uint64_t reserved[4];
} seekdb_plugin_sql_api_v2_t;

/* Optional SQL API minor 2: lookup a standalone FUNCTION (kind=1) or PROCEDURE
 * (kind=2) in the caller's current database and schema view. name is unquoted
 * UTF-8 identifier content, not SQL or a qualified path; 1..2048 bytes, no NUL.
 * Ordinary SHOW visibility applies. Success with object_id=0 means absent;
 * permission failure is not absence. The returned ID is a snapshot, not an
 * execution privilege, dependency, lease or guarantee against concurrent DDL.
 * No new transaction/snapshot, DDL, member adoption or publication is performed.
 * Same callback/thread/reentry and sticky-error rules as execute/poll.
 */
#define SEEKDB_PLUGIN_SQL_CATALOG_LOOKUP_MINOR 2u
typedef struct seekdb_plugin_routine_lookup_result_v1 {
  uint32_t struct_size;
  uint32_t reserved_word;
  int64_t database_error;
  uint64_t object_id;
  uint64_t reserved[4];
} seekdb_plugin_routine_lookup_result_v1_t;
typedef struct seekdb_plugin_sql_api_v3 {
  seekdb_plugin_sql_api_v2_t v2;
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *lookup_routine)(
      seekdb_plugin_sql_context_handle_t *, uint32_t kind, const char *name, uint64_t name_size,
      seekdb_plugin_routine_lookup_result_v1_t *);
  uint64_t reserved[4];
} seekdb_plugin_sql_api_v3_t;

/* Optional SQL API minor 3: execute one UTF-8 standalone routine CREATE,
 * MySQL attribute ALTER, or DROP (including DROP IF EXISTS), at most 4 MiB,
 * without NUL. Uses normal parsing/ACL/definer/dependency/DDL admission and the
 * caller's transaction, NOT an independent install transaction. No implicit
 * commit, schema publication, or automatic Extension membership. Unsupported
 * object classes/multiple statements/CREATE OR REPLACE/CREATE IF NOT EXISTS
 * are rejected; execute() remains SELECT/DML-only.
 *
 * APPLIED means provisional success, never committed/durable success. object_id
 * is zero for an absent DROP IF EXISTS, otherwise the affected routine's ID.
 * Subsequent use requires ordinary resolution/permissions. Every failed call
 * clears the ID. ROLLED_BACK confirms only this operation's rollback; it does
 * not promise that the outer statement/transaction can continue. REQUIRES_ABORT
 * means unsafe cleanup: the host revokes the private view and denies commit.
 * database_error retains the primary error; cleanup failures are separate.
 *
 * Same synchronous thread/context/reentry and sticky invocation errors as SQL.
 * Invalid result buffers fail the invocation on the owner thread. Wrong-thread
 * calls do not touch the context or output. No async/background/retained token.
 */
#define SEEKDB_PLUGIN_SQL_CATALOG_MUTATION_MINOR 3u
typedef enum seekdb_plugin_catalog_outcome {
  SEEKDB_PLUGIN_CATALOG_NOT_STARTED = 0,
  SEEKDB_PLUGIN_CATALOG_APPLIED = 1,
  SEEKDB_PLUGIN_CATALOG_ROLLED_BACK = 2,
  SEEKDB_PLUGIN_CATALOG_REQUIRES_ABORT = 3
} seekdb_plugin_catalog_outcome_t;
typedef struct seekdb_plugin_routine_mutation_result_v1 {
  uint32_t struct_size;
  uint32_t outcome;
  int64_t database_error;
  uint64_t object_id;
  int64_t close_error;
  int64_t identity_error;
  int64_t data_rollback_error;
  int64_t view_rollback_error;
  int64_t poison_error;
  uint64_t reserved[4];
} seekdb_plugin_routine_mutation_result_v1_t;
typedef struct seekdb_plugin_sql_api_v4 {
  seekdb_plugin_sql_api_v3_t v3;
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *mutate_routine)(
      seekdb_plugin_sql_context_handle_t *, const char *sql, uint64_t sql_size,
      seekdb_plugin_routine_mutation_result_v1_t *);
  uint64_t reserved[4];
} seekdb_plugin_sql_api_v4_t;

/* Table service minor 2 opts into this borrowed per-open/next context. It
 * exposes query control only, not SQL execution. The original table prefix
 * and callbacks are unchanged; inspect struct_size before reading the suffix.
 * Old hosts may still provide v1. Never retain this context in a cursor.
 */
typedef struct seekdb_plugin_table_execution_context_v2 {
  seekdb_plugin_table_execution_context_v1_t v1;
  seekdb_plugin_sql_context_handle_t *query_context;
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *poll_query)(
      seekdb_plugin_sql_context_handle_t *, seekdb_plugin_query_status_v1_t *);
  uint64_t reserved[4];
} seekdb_plugin_table_execution_context_v2_t;

/* Table service minor 3 adds synchronous caller-session SQL to the per-open/
 * next context. sql_api uses v2.query_context and has exactly the scalar SQL
 * API's permissions, transaction, error, thread and lifetime rules. No SQL
 * cursor or context may survive a callback. Legacy raw rescan has no context;
 * SQL executors rescan by closing and reopening. Inspect the advertised size
 * before reading this suffix; SQL-dependent services must reject absent SQL.
 * Minor-2 services continue receiving the exact v2 context, not this suffix.
 */
#define SEEKDB_PLUGIN_TABLE_SQL_CONTEXT_MINOR 3u
typedef struct seekdb_plugin_table_execution_context_v3 {
  seekdb_plugin_table_execution_context_v2_t v2;
  const seekdb_plugin_sql_api_v1_t *sql_api;
  uint64_t reserved[4];
} seekdb_plugin_table_execution_context_v3_t;

/* Table service minor 4 requests optional projection metadata for open/next.
 * requested_columns has column_count bytes, each 0 or 1, indexed by the full
 * declared column ordinal (including columns used only by filters). The host
 * borrows it for this callback only; do not retain its address. Missing metadata
 * means all columns are required. A zero-count/null pair means unavailable,
 * not an empty projection; a present all-zero array means row counts only.
 *
 * This is advisory computation pushdown, not a new row format. Emit the same
 * number/order of columns with valid declared representations and nullability;
 * unrequested columns may carry cheap valid placeholder values. Never change
 * row count, ordering, required values or external effects based on projection.
 * Re-read per callback; raw rescan has no projection context. Query control/SQL
 * retain the v2/v3 rules; requesting projection alone does not require SQL.
 * Minor-3 services continue receiving an exact v3 context.
 */
#define SEEKDB_PLUGIN_TABLE_PROJECTION_CONTEXT_MINOR 4u
typedef struct seekdb_plugin_table_execution_context_v4 {
  seekdb_plugin_table_execution_context_v3_t v3;
  uint32_t column_count;
  uint32_t reserved_word;
  const uint8_t *requested_columns;
  uint64_t reserved[4];
} seekdb_plugin_table_execution_context_v4_t;

/* Append-only suffix. Scalar services opt in by setting their execution table
 * spi_minor to SEEKDB_PLUGIN_EXECUTION_SQL_CONTEXT_MINOR (or a later revision).
 * The host supplies an exact v1 prefix copy to spi_minor=0 services, preserving
 * older binaries which check struct_size for equality. Opt-in services still
 * inspect v1.struct_size before casting: an older host/caller can provide v1
 * alone. Reserved fields of v1 retain their original meaning. */
typedef struct seekdb_plugin_execution_context_v2 {
  seekdb_plugin_execution_context_v1_t v1;
  const seekdb_plugin_sql_api_v1_t *sql_api;
  seekdb_plugin_sql_context_handle_t *sql_context;
  uint64_t reserved[4];
} seekdb_plugin_execution_context_v2_t;

#ifdef __cplusplus
}
#endif
#endif
