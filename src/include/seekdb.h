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

#ifndef _SEEKDB_H
#define _SEEKDB_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Type definitions */
// Define my_ulonglong if not already defined (e.g., by ob_mysql_global.h)
// This ensures compatibility with MySQL C API
#ifndef my_ulonglong
#if defined(NO_CLIENT_LONG_LONG)
typedef unsigned long my_ulonglong;
#elif !defined(__WIN__)
typedef unsigned long long my_ulonglong;
#else
typedef unsigned __int64 my_ulonglong;
#endif
#endif

/* Error codes */
#define SEEKDB_SUCCESS 0
#define SEEKDB_ERROR_INVALID_PARAM -1
#define SEEKDB_ERROR_CONNECTION_FAILED -2
#define SEEKDB_ERROR_QUERY_FAILED -3
#define SEEKDB_ERROR_MEMORY_ALLOC -4
#define SEEKDB_ERROR_NOT_INITIALIZED -5

/* Prepared statement fetch result (aligned with MySQL mysql_stmt_fetch) */
#define SEEKDB_NO_DATA 100  /* No more rows; same as MYSQL_NO_DATA */

/* Opaque handle types */
typedef void* SeekdbHandle;
typedef void* SeekdbResult;
typedef void* SeekdbRow;
typedef void* SeekdbStmt;

/**
 * Open an embedded database
 * @param db_dir Database directory path
 * @return SEEKDB_SUCCESS on success, error code otherwise
 */
int seekdb_open(const char* db_dir);

/**
 * Open an embedded database with service (network) support
 * If port > 0, the database will run in server mode (embed_mode = false)
 * If port <= 0, the database will run in embedded mode (embed_mode = true)
 * This matches the behavior of Python embed's open_with_service()
 * @param db_dir Database directory path
 * @param port Port number (0 or negative for embedded mode, > 0 for server mode)
 * @return SEEKDB_SUCCESS on success, error code otherwise
 */
int seekdb_open_with_service(const char* db_dir, int port);

/**
 * Close the embedded database
 */
void seekdb_close(void);

/**
 * Create a new embedded database connection handle
 * @param handle Output parameter for the connection handle
 * @param database Database name
 * @param autocommit Autocommit mode (default: false)
 * @return SEEKDB_SUCCESS on success, error code otherwise
 * @note Concurrent use of the same connection from multiple threads is not guaranteed thread-safe;
 *       use one connection per thread or serialize access.
 */
int seekdb_connect(SeekdbHandle* handle, const char* database, bool autocommit);

/**
 * Close and free a connection handle
 * @param handle Connection handle to close
 */
void seekdb_connect_close(SeekdbHandle handle);

/**
 * Execute a SQL statement (MySQL C API aligned)
 * Executes any SQL: SELECT, INSERT, UPDATE, DELETE, DDL (CREATE/DROP/ALTER), etc.
 * Same as mysql_query() / mysql_real_query(): one API for all statement types.
 * @param handle Connection handle
 * @param query SQL statement string (null-terminated)
 * @param result Output parameter for result handle (may be NULL for DML/DDL; use seekdb_store_result() for SELECT)
 * @return SEEKDB_SUCCESS on success, error code otherwise
 * @note For SELECT: use seekdb_store_result(handle) or the returned result to get rows.
 * @note For INSERT/UPDATE/DELETE/DDL: use seekdb_affected_rows(handle) after execution for affected row count.
 */
int seekdb_query(SeekdbHandle handle, const char* query, SeekdbResult* result);

/**
 * Store result set
 * @param handle Connection handle
 * @return Result handle, or NULL on error
 * @note This function should be called after seekdb_real_query() or seekdb_query()
 * @note The result is automatically stored in the connection, so this function
 *       returns the last result set from the connection
 */
SeekdbResult seekdb_store_result(SeekdbHandle handle);

/**
 * Use result set (streaming mode)
 * @param handle Connection handle
 * @return Result handle, or NULL on error
 * @note This function should be called after seekdb_real_query() or seekdb_query()
 * @note In streaming mode, rows are fetched one at a time from the server
 * @note This is more memory-efficient for large result sets
 */
SeekdbResult seekdb_use_result(SeekdbHandle handle);

/**
 * Execute a SQL query with binary data support
 * This is more efficient than seekdb_query() as it doesn't require strlen()
 * @param handle Connection handle
 * @param stmt_str SQL statement string (may contain binary data)
 * @param length Length of SQL statement string in bytes
 * @param result Output parameter for result handle
 * @return SEEKDB_SUCCESS on success, error code otherwise
 */
int seekdb_real_query(SeekdbHandle handle, const char* stmt_str, unsigned long length, SeekdbResult* result);

/**
 * Get the number of rows in a result set
 * @param result Result handle
 * @return Number of rows, or -1 on error
 */
my_ulonglong seekdb_num_rows(SeekdbResult result);

/**
 * Get the number of columns in a result set
 * @param result Result handle
 * @return Number of columns, or -1 on error
 */
unsigned int seekdb_num_fields(SeekdbResult result);

/**
 * Get the number of result columns for the most recent statement
 * This is similar to mysql_field_count() in MySQL C API
 * @param handle Connection handle
 * @return Number of result columns, or 0 if no result set or error
 */
unsigned int seekdb_field_count(SeekdbHandle handle);

/**
 * Get the length of a column name (without null terminator)
 * @param result Result handle
 * @param column_index Column index (0-based)
 * @return Length of column name, or (size_t)-1 on error
 */
size_t seekdb_result_column_name_len(SeekdbResult result, int32_t column_index);

/**
 * Get column name by index
 * @param result Result handle
 * @param column_index Column index (0-based)
 * @param name Output buffer for column name
 * @param name_len Buffer size
 * @return SEEKDB_SUCCESS on success, error code otherwise
 */
int seekdb_result_column_name(SeekdbResult result, int32_t column_index, char* name, size_t name_len);

/**
 * Fetch the next row from result set
 * Aligned with MySQL: returned row is valid until next seekdb_fetch_row() or seekdb_result_free().
 * @param result Result handle
 * @return SeekdbRow handle if row fetched, NULL if no more rows or error
 * @note When result was obtained via seekdb_use_result(), NULL may mean end of data or error;
 *       use seekdb_errno(handle) or seekdb_error(handle) to distinguish (non-zero = error).
 */
SeekdbRow seekdb_fetch_row(SeekdbResult result);

/**
 * Get the length of a string value (without null terminator)
 * @param row Row handle
 * @param column_index Column index (0-based)
 * @return For NULL column: (size_t)-1. For empty string '': 0. For non-empty: actual byte length.
 *         Returns (size_t)-1 on invalid row/column index.
 */
size_t seekdb_row_get_string_len(SeekdbRow row, int32_t column_index);

/**
 * Get string value from a row by column index
 * @param row Row handle
 * @param column_index Column index (0-based)
 * @param value Output buffer for value
 * @param value_len Buffer size (must be >= seekdb_row_get_string_len()+1 for non-NULL to copy in full; no truncation)
 * @return SEEKDB_SUCCESS on success. For NULL: writes '\\0' and succeeds. For non-NULL: returns error if value_len < len+1.
 *
 * String/JSON column semantics (aligned with seekdb_row_get_string_len and seekdb_row_is_null):
 * - STRING/TEXT: actual byte length is returned; empty string '' has length 0 and seekdb_row_is_null false.
 * - JSON columns (e.g. metadata): empty object "{}" and JSON with special characters (newline, quote, backslash)
 *   are returned as complete, valid JSON strings; seekdb_row_is_null is false. Long JSON may be stored out-of-row
 *   (LOB); length and content are still returned in full. Encoding is UTF-8.
 */
int seekdb_row_get_string(SeekdbRow row, int32_t column_index, char* value, size_t value_len);

/**
 * Get integer value from a row by column index
 * @param row Row handle
 * @param column_index Column index (0-based)
 * @param value Output parameter for integer value
 * @return SEEKDB_SUCCESS on success, error code otherwise
 */
int seekdb_row_get_int64(SeekdbRow row, int32_t column_index, int64_t* value);

/**
 * Get double value from a row by column index
 * @param row Row handle
 * @param column_index Column index (0-based)
 * @param value Output parameter for double value
 * @return SEEKDB_SUCCESS on success, error code otherwise
 */
int seekdb_row_get_double(SeekdbRow row, int32_t column_index, double* value);

/**
 * Get boolean value from a row by column index
 * @param row Row handle
 * @param column_index Column index (0-based)
 * @param value Output parameter for boolean value
 * @return SEEKDB_SUCCESS on success, error code otherwise
 */
int seekdb_row_get_bool(SeekdbRow row, int32_t column_index, bool* value);

/**
 * Check if a value is NULL
 * @param row Row handle
 * @param column_index Column index (0-based)
 * @return true only for SQL NULL; false for empty string '' and non-empty values (call seekdb_row_get_string_len for length)
 */
bool seekdb_row_is_null(SeekdbRow row, int32_t column_index);

/**
 * Free a result handle
 * @param result Result handle to free
 */
void seekdb_result_free(SeekdbResult result);

/**
 * Get the lengths of the columns in the current row
 * This is useful for distinguishing between empty strings and NULL values
 * @param result Result handle
 * @return Array of unsigned long integers representing column lengths, or NULL on error
 * @note The returned array is valid until the next call to seekdb_fetch_row()
 * @note Caller should not free the returned array
 */
unsigned long* seekdb_fetch_lengths(SeekdbResult result);

/**
 * Get the last error message (thread-local, no handle required)
 * @return Pointer to error message string, or NULL if no error
 * @note This is thread-safe and returns the last error for the current thread
 */
const char* seekdb_last_error(void);

/**
 * Get the last error code (thread-local, no handle required)
 * @return Error code, or SEEKDB_SUCCESS if no error
 * @note This is thread-safe and returns the last error code for the current thread
 */
int seekdb_last_error_code(void);

/**
 * Get the last error message
 * @param handle Connection handle
 * @return Pointer to error message string, or NULL if no error
 * @note The returned string is valid until the next API call
 */
const char* seekdb_error(SeekdbHandle handle);

/**
 * Get the last error code
 * @param handle Connection handle
 * @return Error code, or SEEKDB_SUCCESS if no error
 */
unsigned int seekdb_errno(SeekdbHandle handle);

/**
 * Get the number of affected rows (MySQL C API aligned)
 * Use after seekdb_query() with INSERT, UPDATE, DELETE, or DDL (like mysql_affected_rows()).
 * @param handle Connection handle
 * @return Number of affected rows, or 0 if no rows were affected
 */
my_ulonglong seekdb_affected_rows(SeekdbHandle handle);

/**
 * Begin a transaction (SeekDB extension)
 * 
 * This is a SeekDB extension function. In MySQL 5.7 and 8.0 C API, there is no 
 * mysql_begin() function. To begin a transaction in MySQL C API:
 * - Use mysql_autocommit(mysql, 0) to disable autocommit mode, or
 * - Execute "START TRANSACTION" SQL statement using mysql_query() or mysql_real_query()
 * 
 * This function provides a convenient way to start a transaction, equivalent to
 * executing "START TRANSACTION" SQL statement.
 * 
 * @param handle Connection handle
 * @return SEEKDB_SUCCESS on success, error code otherwise
 */
int seekdb_begin(SeekdbHandle handle);

/**
 * Commit a transaction
 * @param handle Connection handle
 * @return SEEKDB_SUCCESS on success, error code otherwise
 */
int seekdb_commit(SeekdbHandle handle);

/**
 * Rollback a transaction
 * @param handle Connection handle
 * @return SEEKDB_SUCCESS on success, error code otherwise
 */
int seekdb_rollback(SeekdbHandle handle);

/**
 * Set autocommit mode for a connection
 * @param handle Connection handle
 * @param mode true to enable autocommit, false to disable
 * @return SEEKDB_SUCCESS on success, error code otherwise
 */
int seekdb_autocommit(SeekdbHandle handle, bool mode);

/**
 * Get the last inserted AUTO_INCREMENT value
 * @param handle Connection handle
 * @return Last inserted ID, or 0 if no AUTO_INCREMENT value was generated
 */
my_ulonglong seekdb_insert_id(SeekdbHandle handle);

/**
 * Check if the connection to the server is alive
 * @param handle Connection handle
 * @return SEEKDB_SUCCESS if connection is alive, error code otherwise
 */
int seekdb_ping(SeekdbHandle handle);

/**
 * Get server version information
 * @param handle Connection handle
 * @return Pointer to version string, or NULL on error
 * @note The returned string is valid until the connection is closed
 */
const char* seekdb_get_server_info(SeekdbHandle handle);

/**
 * Get character set name for the connection
 * @param handle Connection handle
 * @return Pointer to character set name string, or NULL on error
 * @note The returned string is valid until the connection is closed
 */
const char* seekdb_character_set_name(SeekdbHandle handle);

/**
 * Set character set for the connection
 * @param handle Connection handle
 * @param csname Character set name (e.g., "utf8mb4", "gbk")
 * @return SEEKDB_SUCCESS on success, error code otherwise
 */
int seekdb_set_character_set(SeekdbHandle handle, const char* csname);

/**
 * Switch to a different database
 * @param handle Connection handle
 * @param db Database name
 * @return SEEKDB_SUCCESS on success, error code otherwise
 */
int seekdb_select_db(SeekdbHandle handle, const char* db);

/**
 * Get host information for the connection
 * @param handle Connection handle
 * @return Pointer to host information string, or NULL on error
 * @note The returned string is valid until the connection is closed
 */
const char* seekdb_get_host_info(SeekdbHandle handle);

/**
 * Get client version information
 * @return Pointer to client version string
 * @note The returned string is statically allocated
 */
const char* seekdb_get_client_info(void);

/**
 * Get information about the last query
 * @param handle Connection handle
 * @return Pointer to information string, or NULL on error
 * @note The returned string is valid until the next query
 */
const char* seekdb_info(SeekdbHandle handle);

/**
 * Get warning count for the last query
 * @param handle Connection handle
 * @return Number of warnings, or 0 if no warnings
 */
unsigned int seekdb_warning_count(SeekdbHandle handle);

/**
 * Get SQLSTATE for the last error
 * @param handle Connection handle
 * @return Pointer to SQLSTATE string (5 characters), or NULL on error
 * @note The returned string is valid until the next API call
 * @note SQLSTATE is a 5-character string (e.g., "42000" for syntax error)
 */
const char* seekdb_sqlstate(SeekdbHandle handle);

/**
 * Character set information structure
 */
typedef struct {
    uint32_t number;           // Character set number
    const char* name;          // Character set name
    const char* collation;     // Collation name
    const char* comment;       // Comment
    uint32_t dir;              // Directory
    uint32_t min_length;       // Minimum length
    uint32_t max_length;       // Maximum length
} SeekdbCharsetInfo;

/**
 * Get character set information
 * @param handle Connection handle
 * @param csname Character set name
 * @param charset_info Output parameter for character set information
 * @return SEEKDB_SUCCESS on success, error code otherwise
 */
int seekdb_get_character_set_info(SeekdbHandle handle, const char* csname, SeekdbCharsetInfo* charset_info);

/**
 * Escape special characters in a string for use in SQL statements
 * @param handle Connection handle
 * @param to Output buffer for escaped string
 * @param to_len Size of output buffer
 * @param from Source string to escape
 * @param from_len Length of source string
 * @return Length of escaped string, or (unsigned long)-1 on error
 */
unsigned long seekdb_real_escape_string(SeekdbHandle handle, char* to, unsigned long to_len, 
                                         const char* from, unsigned long from_len);

/**
 * Escape special characters in a string for use in SQL statements (with quote context)
 * This is similar to mysql_real_escape_string_quote() in MySQL 8.0+
 * @param handle Connection handle
 * @param to Output buffer for escaped string
 * @param to_len Size of output buffer
 * @param from Source string to escape
 * @param from_len Length of source string
 * @param quote Quote character (' or ") used in the SQL statement
 * @return Length of escaped string, or (unsigned long)-1 on error
 * @note This function considers the quote context, escaping the quote character itself
 * @note If quote is '\'', only single quotes are escaped; if quote is '"', only double quotes are escaped
 */
unsigned long seekdb_real_escape_string_quote(SeekdbHandle handle, char* to, unsigned long to_len,
                                               const char* from, unsigned long from_len, char quote);

/**
 * Convert a string to hexadecimal format
 * @param to Output buffer for hexadecimal string
 * @param to_len Size of output buffer
 * @param from Source string to convert
 * @param from_len Length of source string
 * @return Length of hexadecimal string, or (unsigned long)-1 on error
 */
unsigned long seekdb_hex_string(char* to, unsigned long to_len, const char* from, unsigned long from_len);

/**
 * Get server version number
 * @param handle Connection handle
 * @return Server version number, or 0 on error
 * @note Version number format: major * 10000 + minor * 100 + patch
 */
unsigned long seekdb_get_server_version(SeekdbHandle handle);

/**
 * Change user for the connection
 * @param handle Connection handle
 * @param user User name
 * @param password Password
 * @param database Database name
 * @return SEEKDB_SUCCESS on success, error code otherwise
 * @note For embedded mode, this may require reconnection
 */
int seekdb_change_user(SeekdbHandle handle, const char* user, const char* password, const char* database);

/**
 * Reset connection to clear session state
 * This is similar to mysql_reset_connection() in MySQL C API
 * @param handle Connection handle
 * @return SEEKDB_SUCCESS on success, error code otherwise
 * @note This resets session variables, temporary tables, locks, and other session state
 * @note Useful for connection pool scenarios and error recovery
 */
int seekdb_reset_connection(SeekdbHandle handle);

/**
 * Process next result set (for multiple result sets)
 * @param handle Connection handle
 * @return 0 if more results, -1 if no more results, positive value on error
 */
int seekdb_next_result(SeekdbHandle handle);

/**
 * Check if there are more result sets
 * @param handle Connection handle
 * @return true if more results, false otherwise
 */
bool seekdb_more_results(SeekdbHandle handle);

/* Prepared Statement Types */

/**
 * Parameter binding buffer structure
 * Similar to MYSQL_BIND in MySQL C API
 */
// Field type enumeration (similar to MySQL field types)
typedef enum {
    SEEKDB_TYPE_NULL = 0,
    SEEKDB_TYPE_TINY = 1,
    SEEKDB_TYPE_SHORT = 2,
    SEEKDB_TYPE_LONG = 3,
    SEEKDB_TYPE_LONGLONG = 4,
    SEEKDB_TYPE_FLOAT = 5,
    SEEKDB_TYPE_DOUBLE = 6,
    SEEKDB_TYPE_TIME = 7,
    SEEKDB_TYPE_DATE = 8,
    SEEKDB_TYPE_DATETIME = 9,
    SEEKDB_TYPE_TIMESTAMP = 10,
    SEEKDB_TYPE_STRING = 11,
    SEEKDB_TYPE_BLOB = 12,
    SEEKDB_TYPE_VECTOR = 13,  // VECTOR type: input as JSON array '[1,2,3]', stored as binary (float array)
    SEEKDB_TYPE_VARBINARY_ID = 14  // VARBINARY(512) _id: right-pad/truncate to 512 bytes, output as 0x hex. Use for _id placeholders (no SQL parsing; semantics from bind type, like MySQL)
} SeekdbFieldType;

typedef struct {
    SeekdbFieldType buffer_type;
    void* buffer;              // Data buffer
    unsigned long buffer_length; // Buffer length
    unsigned long* length;     // Data length
    bool* is_null;             // NULL indicator
    bool* error;               // Error indicator
    unsigned char* is_unsigned; // Unsigned flag (for integer types)
} SeekdbBind;

/**
 * Execute a SQL query with parameters (convenience function)
 * This function internally uses prepared statements (aligned with MySQL C API)
 * For better performance with repeated queries, use seekdb_stmt_* functions directly
 * @param handle Connection handle
 * @param query SQL query string with ? placeholders (null-terminated)
 * @param result Output parameter for result handle
 * @param bind Array of SeekdbBind structures (similar to MYSQL_BIND in MySQL C API)
 * @param param_count Number of parameters
 * @return SEEKDB_SUCCESS on success, error code otherwise
 * @note This function creates a temporary prepared statement internally
 * @note For better performance, use seekdb_stmt_init(), seekdb_stmt_prepare(), 
 *       seekdb_stmt_bind_param(), and seekdb_stmt_execute() directly
 */
int seekdb_query_with_params(
    SeekdbHandle handle,
    const char* query,
    SeekdbResult* result,
    SeekdbBind* bind,
    unsigned int param_count
);

/**
 * Execute a SQL query with parameters and binary data support (convenience function)
 * This function internally uses prepared statements (aligned with MySQL C API)
 * @param handle Connection handle
 * @param stmt_str SQL statement string with ? placeholders (may contain binary data)
 * @param length Length of SQL statement string in bytes
 * @param result Output parameter for result handle
 * @param bind Array of SeekdbBind structures (similar to MYSQL_BIND in MySQL C API)
 * @param param_count Number of parameters
 * @return SEEKDB_SUCCESS on success, error code otherwise
 * @note This function creates a temporary prepared statement internally
 */
int seekdb_real_query_with_params(
    SeekdbHandle handle,
    const char* stmt_str,
    unsigned long length,
    SeekdbResult* result,
    SeekdbBind* bind,
    unsigned int param_count
);

/**
 * Initialize a prepared statement handle
 * @param handle Connection handle
 * @return Prepared statement handle, or NULL on error
 */
SeekdbStmt seekdb_stmt_init(SeekdbHandle handle);

/**
 * Prepare a SQL statement
 * @param stmt Prepared statement handle
 * @param query SQL query string with placeholders (?)
 * @param length Length of query string
 * @return SEEKDB_SUCCESS on success, error code otherwise
 */
int seekdb_stmt_prepare(SeekdbStmt stmt, const char* query, unsigned long length);

/**
 * Bind parameters to a prepared statement
 * @param stmt Prepared statement handle
 * @param bind Array of SeekdbBind structures
 * @return SEEKDB_SUCCESS on success, error code otherwise
 */
int seekdb_stmt_bind_param(SeekdbStmt stmt, SeekdbBind* bind);

/**
 * Execute a prepared statement
 * @param stmt Prepared statement handle
 * @return SEEKDB_SUCCESS on success, error code otherwise
 */
int seekdb_stmt_execute(SeekdbStmt stmt);

/**
 * Bind result columns to buffers
 * @param stmt Prepared statement handle
 * @param bind Array of SeekdbBind structures
 * @return SEEKDB_SUCCESS on success, error code otherwise
 */
int seekdb_stmt_bind_result(SeekdbStmt stmt, SeekdbBind* bind);

/**
 * Fetch the next row from a prepared statement result set
 * Aligned with MySQL mysql_stmt_fetch() return values.
 * @param stmt Prepared statement handle
 * @return 0 on success, SEEKDB_NO_DATA if no more rows, 1 or SEEKDB_ERROR_* on error
 */
int seekdb_stmt_fetch(SeekdbStmt stmt);

/**
 * Get result set metadata for a prepared statement
 * Aligned with MySQL: mysql_stmt_result_metadata() returns MYSQL_RES that caller must mysql_free_result().
 * @param stmt Prepared statement handle
 * @return Result handle with metadata (caller-owned), or NULL on error
 * @note Caller must call seekdb_result_free() on the returned result when done.
 */
SeekdbResult seekdb_stmt_result_metadata(SeekdbStmt stmt);

/**
 * Get the number of parameters in a prepared statement
 * @param stmt Prepared statement handle
 * @return Number of parameters, or 0 on error
 */
unsigned long seekdb_stmt_param_count(SeekdbStmt stmt);

/**
 * Get the number of affected rows from the last executed statement
 * @param stmt Prepared statement handle
 * @return Number of affected rows
 */
my_ulonglong seekdb_stmt_affected_rows(SeekdbStmt stmt);

/**
 * Get the last inserted AUTO_INCREMENT value
 * @param stmt Prepared statement handle
 * @return Last inserted ID, or 0 if no AUTO_INCREMENT value was generated
 */
my_ulonglong seekdb_stmt_insert_id(SeekdbStmt stmt);

/**
 * Get the number of rows in the result set
 * @param stmt Prepared statement handle
 * @return Number of rows, or 0 on error
 */
my_ulonglong seekdb_stmt_num_rows(SeekdbStmt stmt);

/**
 * Free result set from a prepared statement
 * @param stmt Prepared statement handle
 * @return SEEKDB_SUCCESS on success, error code otherwise
 */
int seekdb_stmt_free_result(SeekdbStmt stmt);

/**
 * Close and free a prepared statement handle
 * @param stmt Prepared statement handle to close
 */
void seekdb_stmt_close(SeekdbStmt stmt);

/**
 * Get error message for a prepared statement
 * @param stmt Prepared statement handle
 * @return Pointer to error message string, or NULL if no error
 */
const char* seekdb_stmt_error(SeekdbStmt stmt);

/**
 * Get error code for a prepared statement
 * @param stmt Prepared statement handle
 * @return Error code, or SEEKDB_SUCCESS if no error
 */
unsigned int seekdb_stmt_errno(SeekdbStmt stmt);

/**
 * Get SQLSTATE value for a prepared statement
 * @param stmt Prepared statement handle
 * @return Pointer to SQLSTATE string (5 characters), or NULL on error
 * @note The returned string is valid until the next API call
 * @note SQLSTATE is a 5-character string (e.g., "42000" for syntax error)
 */
const char* seekdb_stmt_sqlstate(SeekdbStmt stmt);

/**
 * Reset a prepared statement
 * This resets the statement buffers on the server side and clears any error state
 * @param stmt Prepared statement handle
 * @return SEEKDB_SUCCESS on success, error code otherwise
 * @note After reset, the statement can be re-executed with new parameters
 */
int seekdb_stmt_reset(SeekdbStmt stmt);

/**
 * Get the number of result columns for a prepared statement
 * @param stmt Prepared statement handle
 * @return Number of result columns, or 0 if no result set or error
 */
unsigned int seekdb_stmt_field_count(SeekdbStmt stmt);

/**
 * Seek to arbitrary row number in prepared statement result set
 * @param stmt Prepared statement handle
 * @param offset Row offset (0-based)
 * @return SEEKDB_SUCCESS on success, error code otherwise
 */
int seekdb_stmt_data_seek(SeekdbStmt stmt, my_ulonglong offset);

/**
 * Seek to row position in prepared statement result set
 * This is similar to mysql_stmt_row_seek() in MySQL C API
 * @param stmt Prepared statement handle
 * @param row Row handle from previous seekdb_stmt_row_seek() call
 * @return Row handle at the saved position, or NULL on error
 * @note This function allows saving and restoring row positions
 */
SeekdbRow seekdb_stmt_row_seek(SeekdbStmt stmt, SeekdbRow row);

/**
 * Get current row position in prepared statement result set
 * This is similar to mysql_stmt_row_tell() in MySQL C API
 * @param stmt Prepared statement handle
 * @return Current row handle, or NULL on error
 * @note The returned row handle can be used with seekdb_stmt_row_seek()
 */
SeekdbRow seekdb_stmt_row_tell(SeekdbStmt stmt);

/**
 * Fetch data for one column of current result set row
 * This is similar to mysql_stmt_fetch_column() in MySQL C API
 * @param stmt Prepared statement handle
 * @param bind SeekdbBind structure for the column
 * @param column_index Column index (0-based)
 * @param offset Offset within the column data (for partial fetch)
 * @return SEEKDB_SUCCESS on success, error code otherwise
 */
int seekdb_stmt_fetch_column(SeekdbStmt stmt, SeekdbBind* bind, unsigned int column_index, unsigned long offset);

/**
 * Get parameter metadata for a prepared statement
 * This is similar to mysql_stmt_param_metadata() in MySQL C API
 * @param stmt Prepared statement handle
 * @return Result handle with parameter metadata, or NULL on error
 * @note The result contains one row per parameter with metadata information
 * @note Each row contains: parameter name, type, length, flags, etc.
 */
SeekdbResult seekdb_stmt_param_metadata(SeekdbStmt stmt);

/**
 * Store result set from a prepared statement
 * This is similar to mysql_stmt_store_result() in MySQL C API
 * @param stmt Prepared statement handle
 * @return SEEKDB_SUCCESS on success, error code otherwise
 * @note This retrieves and stores the entire result set in memory
 * @note After calling this, you can use seekdb_stmt_num_rows() to get row count
 * @note This is useful for buffered result sets (similar to mysql_store_result())
 */
int seekdb_stmt_store_result(SeekdbStmt stmt);

/**
 * Process next result set from a prepared statement
 * This is similar to mysql_stmt_next_result() in MySQL C API
 * @param stmt Prepared statement handle
 * @return 0 if more results, -1 if no more results, error code otherwise
 * @note Used for stored procedures that return multiple result sets
 */
int seekdb_stmt_next_result(SeekdbStmt stmt);

/**
 * Callback function type for fetching all rows
 * @param row_index Row index (0-based)
 * @param column_index Column index (0-based)
 * @param is_null Whether the value is NULL
 * @param value String value (NULL if is_null is true, otherwise null-terminated)
 * @param value_len Length of value string (0 if is_null is true)
 * @param user_data User-provided data pointer
 * @return 0 to continue, non-zero to stop
 */
typedef int (*seekdb_cell_callback_t)(
    int64_t row_index,
    int32_t column_index,
    bool is_null,
    const char* value,
    size_t value_len,
    void* user_data
);

/**
 * Fetch all rows from a result set using a callback function
 * This avoids repetitive loop logic in language bindings
 * @param result Result handle
 * @param callback Callback function called for each cell (row, column)
 * @param user_data User data passed to callback
 * @return SEEKDB_SUCCESS on success, error code otherwise
 */
int seekdb_result_fetch_all(
    SeekdbResult result,
    seekdb_cell_callback_t callback,
    void* user_data
);

/**
 * Get all column names from a result set
 * @param result Result handle
 * @param names Output array of column name pointers (must have at least column_count elements)
 * @param name_bufs Pre-allocated buffer for column names (must be at least column_count * name_buf_size bytes)
 * @param name_buf_size Size of each name buffer
 * @param column_count Input: maximum columns to retrieve, Output: actual number of columns
 * @return SEEKDB_SUCCESS on success, error code otherwise
 * 
 * @note Column names are stored sequentially in name_bufs:
 *   names[0] = name_bufs + 0 * name_buf_size
 *   names[1] = name_bufs + 1 * name_buf_size
 *   ...
 */
int seekdb_result_get_all_column_names(
    SeekdbResult result,
    char** names,
    char* name_bufs,
    size_t name_buf_size,
    int32_t* column_count
);

/**
 * Get all column names from a result set (convenience function)
 * This function allocates memory for column names internally
 * @param result Result handle
 * @param names Output array of column name pointers (caller must free using seekdb_free_column_names())
 * @param column_count Output parameter for number of columns
 * @return SEEKDB_SUCCESS on success, error code otherwise
 * @note Caller must call seekdb_free_column_names() to free the allocated memory
 */
int seekdb_result_get_all_column_names_alloc(
    SeekdbResult result,
    char*** names,
    int32_t* column_count
);

/**
 * Free column names allocated by seekdb_result_get_all_column_names_alloc()
 * @param names Column name array returned by seekdb_result_get_all_column_names_alloc()
 * @param column_count Number of columns
 */
void seekdb_free_column_names(char** names, int32_t column_count);

/**
 * Field information structure
 * Provides metadata about a column in the result set
 */
typedef struct {
    const char* name;           // Column name
    const char* org_name;       // Original column name (if aliased)
    const char* table;         // Table name
    const char* org_table;      // Original table name
    const char* db;             // Database name
    const char* catalog;       // Catalog name
    const char* def;            // Default value (NULL if not applicable)
    uint32_t length;            // Column width
    uint32_t max_length;        // Maximum width of selected set
    uint32_t name_length;       // Name length
    uint32_t org_name_length;   // Original name length
    uint32_t table_length;      // Table name length
    uint32_t org_table_length;  // Original table name length
    uint32_t db_length;         // Database name length
    uint32_t catalog_length;    // Catalog name length
    uint32_t def_length;        // Default value length
    uint32_t flags;             // Div flags (e.g., NOT_NULL_FLAG, PRI_KEY_FLAG)
    uint32_t decimals;          // Number of decimals in field
    uint32_t charsetnr;         // Character set number
    int32_t type;               // Type of field
    void* extension;            // Extension for future use (NULL)
} SeekdbField;

/**
 * Get all field information
 * This provides complete metadata about all columns in a single call
 * @param result Result handle
 * @return Array of SeekdbField structures, or NULL on error
 * @note The returned array is valid until seekdb_result_free() is called
 * @note Caller should not free the returned array
 */
SeekdbField* seekdb_fetch_fields(SeekdbResult result);

/**
 * Get field information for the next field
 * @param result Result handle
 * @return Pointer to SeekdbField structure, or NULL if no more fields
 * @note The returned pointer is valid until the next call to seekdb_fetch_field()
 * @note Caller should not free the returned pointer
 */
SeekdbField* seekdb_fetch_field(SeekdbResult result);

/**
 * Get field information by column index
 * @param result Result handle
 * @param fieldnr Field index (0-based)
 * @return Pointer to SeekdbField structure, or NULL on error
 * @note The returned pointer is valid until seekdb_result_free() is called
 * @note Caller should not free the returned pointer
 */
SeekdbField* seekdb_fetch_field_direct(SeekdbResult result, unsigned int fieldnr);

/**
 * Set field position for seekdb_fetch_field()
 * @param result Result handle
 * @param offset Field offset (0-based)
 * @return Previous field position, or -1 on error
 */
int seekdb_field_seek(SeekdbResult result, unsigned int offset);

/**
 * Get current field position for seekdb_fetch_field()
 * @param result Result handle
 * @return Current field position (0-based), or -1 on error
 */
unsigned int seekdb_field_tell(SeekdbResult result);

/**
 * Jump to a specific row in the result set
 * @param result Result handle
 * @param offset Row offset (0-based)
 * @return SEEKDB_SUCCESS on success, error code otherwise
 */
int seekdb_data_seek(SeekdbResult result, my_ulonglong offset);

/**
 * Get current row position
 * @param result Result handle
 * @return Current row position (0-based), or -1 on error
 */
my_ulonglong seekdb_row_tell(SeekdbResult result);

/**
 * Set row position (returns row handle at that position)
 * @param result Result handle
 * @param row Row handle from previous seekdb_row_seek() call
 * @return Row handle at the saved position, or NULL on error
 */
SeekdbRow seekdb_row_seek(SeekdbResult result, SeekdbRow row);

/**
 * Fetch all rows as an array of string arrays
 * Each row is represented as char*[], where NULL pointer indicates NULL value
 * @param result Result handle
 * @param rows Output array of row data pointers (each row is char*[])
 * @param row_count Output parameter for number of rows
 * @param column_count Output parameter for number of columns
 * @return SEEKDB_SUCCESS on success, error code otherwise
 * 
 * @note Memory management: caller must free using seekdb_result_free_all_rows()
 * @note NULL pointer in row array indicates NULL value
 * @note This is more efficient than calling seekdb_fetch_row() multiple times
 */
int seekdb_result_fetch_all_rows(
    SeekdbResult result,
    char*** rows,
    uint64_t* row_count,
    uint32_t* column_count
);

/**
 * Free all rows fetched by seekdb_result_fetch_all_rows()
 * @param rows Row data array returned by seekdb_result_fetch_all_rows()
 * @param row_count Number of rows
 * @param column_count Number of columns
 */
void seekdb_result_free_all_rows(
    char** rows,
    uint64_t row_count,
    uint32_t column_count
);

#ifdef __cplusplus
}
#endif

#endif /* _SEEKDB_H */

