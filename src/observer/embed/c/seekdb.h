/*
 * Copyright (c) 2025 OceanBase.
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

/*
 * SeekDB Embedded C API
 *
 * Language-agnostic C interface for embedding SeekDB in-process.
 * All functions return 0 on success, negative error code on failure.
 */
#pragma once

/* SeekDB return codes */
#define SEEKDB_OK    0
#define SEEKDB_ERROR 1
#define SEEKDB_ROW   100
#define SEEKDB_DONE  101

#ifdef __cplusplus
extern "C" {
#endif

typedef struct seekdb_t* seekdb_handle;
typedef struct seekdb_conn_t* seekdb_conn_handle;
typedef struct seekdb_result_t* seekdb_result_handle;

/* Lifecycle */
int seekdb_open(const char* db_dir, seekdb_handle* out);
int seekdb_open_with_service(const char* db_dir, int port, seekdb_handle* out);
void seekdb_close(seekdb_handle db);

/* Connection */
int seekdb_connect(seekdb_handle db, const char* db_name, seekdb_conn_handle* out);
int seekdb_connect_ex(seekdb_handle db, const char* db_name, int autocommit, seekdb_conn_handle* out);
void seekdb_disconnect(seekdb_conn_handle conn);

/* Query execution */
int seekdb_execute(seekdb_conn_handle conn, const char* sql, seekdb_result_handle* out);
void seekdb_result_free(seekdb_result_handle result);

/* Result access */
int seekdb_result_column_count(seekdb_result_handle result);
const char* seekdb_result_column_name(seekdb_result_handle result, int col);
int seekdb_result_row_count(seekdb_result_handle result);
const char* seekdb_result_value(seekdb_result_handle result, int row, int col);
int seekdb_result_affected_rows(seekdb_result_handle result);

/* Error */
const char* seekdb_error(seekdb_handle db);

/* Statement API (SQLite-style prepare/step) */
typedef struct seekdb_stmt_t* seekdb_stmt_handle;

int seekdb_prepare(seekdb_conn_handle conn, const char* sql, seekdb_stmt_handle* out);
int seekdb_step(seekdb_stmt_handle stmt);
int seekdb_reset(seekdb_stmt_handle stmt);
int seekdb_finalize(seekdb_stmt_handle stmt);

/* Parameter binding */
int seekdb_bind_int(seekdb_stmt_handle stmt, int col, int val);
int seekdb_bind_int64(seekdb_stmt_handle stmt, int col, long long val);
int seekdb_bind_double(seekdb_stmt_handle stmt, int col, double val);
int seekdb_bind_text(seekdb_stmt_handle stmt, int col, const char* val);
int seekdb_bind_null(seekdb_stmt_handle stmt, int col);
int seekdb_bind_parameter_count(seekdb_stmt_handle stmt);

/* Result access (after step) */
int seekdb_column_count(seekdb_stmt_handle stmt);
const char* seekdb_column_name(seekdb_stmt_handle stmt, int col);
int seekdb_column_type(seekdb_stmt_handle stmt, int col);
int seekdb_column_int(seekdb_stmt_handle stmt, int col);
long long seekdb_column_int64(seekdb_stmt_handle stmt, int col);
double seekdb_column_double(seekdb_stmt_handle stmt, int col);
const char* seekdb_column_text(seekdb_stmt_handle stmt, int col);
int seekdb_column_bytes(seekdb_stmt_handle stmt, int col);

/* Utility */
const char* seekdb_errmsg(seekdb_conn_handle conn);
const char* seekdb_libversion(void);

#ifdef __cplusplus
}
#endif
