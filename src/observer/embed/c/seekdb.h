/*
 * SeekDB Embedded C API
 *
 * Language-agnostic C interface for embedding SeekDB in-process.
 * All functions return 0 on success, negative error code on failure.
 */
#pragma once

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

#ifdef __cplusplus
}
#endif
