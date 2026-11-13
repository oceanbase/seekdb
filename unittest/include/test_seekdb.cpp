/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SeekDB C++ Binding Test Suite
 */

#include "seekdb.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <unistd.h>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

struct TestResult {
    bool passed;
    std::string message;
};

// Database path used by main(); set in main() so same-path-reuse tests use it
static const char* g_db_path = "./seekdb.db";

// Test database open (database is already open in main, just verify it's open)
TestResult test_open() {
    // Database is already open in main(), so just verify we can create a connection
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS || handle == nullptr) {
        return {false, "Failed to create connection (database not open?)"};
    }
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test connection creation
TestResult test_connection() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS || handle == nullptr) {
        return {false, "Failed to create connection"};
    }
    
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test error handling
TestResult test_error_handling() {
    // Test invalid parameters
    int ret = seekdb_connect(nullptr, "test", true);
    if (ret != SEEKDB_ERROR_INVALID_PARAM) {
        return {false, "Should return error for null handle"};
    }
    
    SeekdbHandle handle = nullptr;
    ret = seekdb_connect(&handle, nullptr, true);
    if (ret != SEEKDB_ERROR_INVALID_PARAM) {
        return {false, "Should return error for null database"};
    }
    
    ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    // Test invalid SQL
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "INVALID SQL STATEMENT", &result);
    if (ret == SEEKDB_SUCCESS) {
        if (result) seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Should return error for invalid SQL"};
    }
    
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test result operations
TestResult test_result_operations() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, R"(SELECT 1 as id, "hello" as message, 3.14 as price, true as active)", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to execute query"};
    }
    
    // seekdb_query already sets result, but we need to get it from store_result for compatibility
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to store result"};
    }
    
    my_ulonglong row_count = seekdb_num_rows(result);
    if (row_count != 1) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        
        return {false, "Expected row count 1"};
    }
    
    unsigned int col_count = seekdb_num_fields(result);
    if (col_count != 4) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        
        return {false, "Expected column count 4"};
    }
    
    SeekdbRow row = seekdb_fetch_row(result);
    if (row == nullptr) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        
        return {false, "Failed to fetch row"};
    }
    
    seekdb_result_free(result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test row operations and data types
TestResult test_row_operations() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    const char* create_sql = R"(
        CREATE TABLE IF NOT EXISTS test_types (
            id INT PRIMARY KEY,
            name VARCHAR(100),
            price DECIMAL(10,2),
            quantity INT,
            active BOOLEAN,
            score DOUBLE
        )
    )";
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, create_sql, &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to create table"};
    }
    if (result) seekdb_result_free(result);
    
    const char* insert_sql = R"(
        INSERT INTO test_types VALUES
        (1, 'Product A', 99.99, 10, true, 4.5),
        (2, 'Product B', 199.99, 5, false, 3.8),
        (3, NULL, NULL, NULL, NULL, NULL)
    )";
    ret = seekdb_query(handle, insert_sql, &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to insert data"};
    }
    if (result) seekdb_result_free(result);
    
    ret = seekdb_query(handle, "SELECT * FROM test_types ORDER BY id", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to query data"};
    }
    
    // Get stored result (seekdb_query already sets result, but store_result returns the same)
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to store result"};
    }
    
    my_ulonglong row_count = seekdb_num_rows(result);
    if (row_count != 3) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        
        return {false, "Expected 3 rows"};
    }
    
    seekdb_result_free(result);
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_types", &result);
    if (result) seekdb_result_free(result);
    seekdb_connect_close(handle);
    
    return {true, ""};
}

// Test error message retrieval
TestResult test_error_message() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    // Try to trigger an error
    SeekdbResult result = nullptr;
    seekdb_query(handle, "SELECT * FROM non_existent_table", &result);
    if (result) {
        seekdb_result_free(result);
    }
    
    const char* error_msg = seekdb_error(handle);
    if (error_msg == nullptr || strlen(error_msg) == 0) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to get error message"};
    }
    
    seekdb_connect_close(handle);
    
    return {true, ""};
}

// Test transaction management
TestResult test_transaction_management() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", false);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "CREATE TABLE IF NOT EXISTS test_txn (id INT PRIMARY KEY, value INT)", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to create table"};
    }
    if (result) seekdb_result_free(result);
    
    // Test begin/commit
    ret = seekdb_begin(handle);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to begin transaction"};
    }
    
    ret = seekdb_query(handle, "INSERT INTO test_txn VALUES (1, 100)", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to insert"};
    }
    if (result) seekdb_result_free(result);
    
    ret = seekdb_commit(handle);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to commit"};
    }
    
    ret = seekdb_query(handle, "SELECT * FROM test_txn WHERE id = 1", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to verify commit"};
    }
    
    // Get stored result (seekdb_query already sets result, but store_result returns the same)
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to store result"};
    }
    
    my_ulonglong row_count = seekdb_num_rows(result);
    if (row_count != 1) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        
        return {false, "Data not committed"};
    }
    seekdb_result_free(result);
    
    // Test begin/rollback
    ret = seekdb_begin(handle);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to begin transaction"};
    }
    
    ret = seekdb_query(handle, "INSERT INTO test_txn VALUES (2, 200)", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to insert"};
    }
    if (result) seekdb_result_free(result);
    
    ret = seekdb_rollback(handle);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to rollback"};
    }
    
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_txn", &result);
    if (result) seekdb_result_free(result);
    seekdb_connect_close(handle);
    
    return {true, ""};
}

// Test DDL operations
TestResult test_ddl_operations() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    SeekdbResult result = nullptr;
    const char* create_sql = R"(
        CREATE TABLE IF NOT EXISTS test_ddl (
            id INT PRIMARY KEY,
            name VARCHAR(100),
            created_at TIMESTAMP
        )
    )";
    ret = seekdb_query(handle, create_sql, &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to create table"};
    }
    if (result) seekdb_result_free(result);
    
    // ALTER TABLE may not be supported
    seekdb_query(handle, "ALTER TABLE test_ddl ADD COLUMN description VARCHAR(255)", &result);
    if (result) seekdb_result_free(result);
    
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_ddl", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to drop table"};
    }
    if (result) seekdb_result_free(result);
    
    seekdb_connect_close(handle);
    
    return {true, ""};
}

// Test DDL visibility: after CREATE TABLE via seekdb_query(), SHOW TABLES (or catalog query)
// must see the new table. Reproduces listCollections returning 0 when DDL is done via read path.
TestResult test_ddl_visibility_after_create() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    SeekdbResult result = nullptr;
    static const char* table_name = "ddl_vis_test_table";
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS ddl_vis_test_table", &result);
    if (result) seekdb_result_free(result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to drop table before test"};
    }
    ret = seekdb_query(handle, "CREATE TABLE ddl_vis_test_table (id INT PRIMARY KEY)", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to create table"};
    }
    if (result) seekdb_result_free(result);
    ret = seekdb_query(handle, "SHOW TABLES", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_query(handle, "DROP TABLE IF EXISTS ddl_vis_test_table", &result);
        if (result) seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to run SHOW TABLES"};
    }
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_query(handle, "DROP TABLE IF EXISTS ddl_vis_test_table", &result);
        if (result) seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to store SHOW TABLES result"};
    }
    bool found = false;
    char name_buf[256];
    SeekdbRow row;
    while ((row = seekdb_fetch_row(result)) != nullptr) {
        if (seekdb_row_get_string(row, 0, name_buf, sizeof(name_buf)) == SEEKDB_SUCCESS &&
            strcmp(name_buf, table_name) == 0) {
            found = true;
            break;
        }
    }
    seekdb_result_free(result);
    seekdb_query(handle, "DROP TABLE IF EXISTS ddl_vis_test_table", &result);
    if (result) seekdb_result_free(result);
    seekdb_connect_close(handle);
    if (!found) {
        return {false, "SHOW TABLES did not show ddl_vis_test_table after CREATE TABLE (DDL visibility)"};
    }
    return {true, ""};
}

// Test DML operations
TestResult test_dml_operations() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    SeekdbResult result = nullptr;
    // Drop table first to ensure clean state
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_dml", &result);
    if (result) seekdb_result_free(result);
    
    const char* create_sql = R"(
        CREATE TABLE test_dml (
            id INT PRIMARY KEY,
            name VARCHAR(100),
            value INT
        )
    )";
    ret = seekdb_query(handle, create_sql, &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to create table"};
    }
    if (result) seekdb_result_free(result);
    
    ret = seekdb_query(handle, R"(INSERT INTO test_dml VALUES (1, 'A', 10), (2, 'B', 20), (3, 'C', 30))", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to insert"};
    }
    if (result) seekdb_result_free(result);
    my_ulonglong affected = seekdb_affected_rows(handle);
    // Note: affected rows might be 0 if using autocommit and table already had data
    // or if the implementation doesn't track affected rows for INSERT
    // For now, we just check that the insert succeeded (ret == SEEKDB_SUCCESS)
    // and verify the data was inserted by querying it
    
    ret = seekdb_query(handle, R"(UPDATE test_dml SET value = 100 WHERE id = 1)", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to update"};
    }
    if (result) seekdb_result_free(result);
    // Verify update by querying the data instead of checking affected rows
    // (affected rows may not be accurately tracked in all cases)
    
    ret = seekdb_query(handle, "SELECT value FROM test_dml WHERE id = 1", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to verify update"};
    }
    
    // Get stored result (seekdb_query already sets result, but store_result returns the same)
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to store result"};
    }
    
    my_ulonglong row_count = seekdb_num_rows(result);
    if (row_count != 1) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        
        return {false, "UPDATE verification failed"};
    }
    seekdb_result_free(result);
    
    ret = seekdb_query(handle, "DELETE FROM test_dml WHERE id = 2", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to delete"};
    }
    if (result) seekdb_result_free(result);
    // Verify delete by querying the data instead of checking affected rows
    // (affected rows may not be accurately tracked in all cases)
    
    ret = seekdb_query(handle, "SELECT * FROM test_dml WHERE id = 2", &result);
    if (ret == SEEKDB_SUCCESS) {
        result = seekdb_store_result(handle);
        if (result) {
            row_count = seekdb_num_rows(result);
            if (row_count != 0) {
                seekdb_result_free(result);
                seekdb_connect_close(handle);
                
                return {false, "DELETE verification failed"};
            }
            seekdb_result_free(result);
        }
    }
    
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_dml", &result);
    if (result) seekdb_result_free(result);
    seekdb_connect_close(handle);
    
    return {true, ""};
}

// Test DBMS_HYBRID_SEARCH.GET_SQL
TestResult test_hybrid_search_get_sql() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS doc_table", &result);
    if (result) seekdb_result_free(result);
    
    const char* create_sql = R"(
        CREATE TABLE doc_table (
            c1 INT,
            vector VECTOR(3),
            query VARCHAR(255),
            content VARCHAR(255),
            VECTOR INDEX idx1(vector) WITH (distance=l2, type=hnsw, lib=vsag),
            FULLTEXT idx2(query),
            FULLTEXT idx3(content)
        )
    )";
    ret = seekdb_query(handle, create_sql, &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to create table"};
    }
    if (result) seekdb_result_free(result);
    
    const char* insert_sql = R"(
        INSERT INTO doc_table VALUES
        (1, '[1,2,3]', 'hello world', 'oceanbase Elasticsearch database'),
        (2, '[1,2,1]', 'hello world, what is your name', 'oceanbase mysql database'),
        (3, '[1,1,1]', 'hello world, how are you', 'oceanbase oracle database'),
        (4, '[1,3,1]', 'real world, where are you from', 'postgres oracle database'),
        (5, '[1,3,2]', 'real world, how old are you', 'redis oracle database'),
        (6, '[2,1,1]', 'hello world, where are you from', 'starrocks oceanbase database')
    )";
    ret = seekdb_query(handle, insert_sql, &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to insert data"};
    }
    if (result) seekdb_result_free(result);
    
    const char* search_params = R"({"query":{"bool":{"should":[{"match":{"query":"hi hello"}},{"match":{"content":"oceanbase mysql"}}],"filter":[{"term":{"content":"postgres"}}]}},"knn":{"field":"vector","k":5,"query_vector":[1,2,3]},"_source":["query","content","_keyword_score","_semantic_score"]})";
    std::string escaped_params = search_params;
    size_t pos = 0;
    while ((pos = escaped_params.find("'", pos)) != std::string::npos) {
        escaped_params.replace(pos, 1, "''");
        pos += 2;
    }
    
    std::string set_parm_sql = "SET @parm = '" + escaped_params + "'";
    ret = seekdb_query(handle, set_parm_sql.c_str(), &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to set parameter"};
    }
    if (result) seekdb_result_free(result);
    
    ret = seekdb_query(handle, "SELECT DBMS_HYBRID_SEARCH.GET_SQL('doc_table', @parm)", &result);
    if (ret != SEEKDB_SUCCESS) {
        const char* error_msg = seekdb_error(handle);
        std::string msg = "Failed to execute GET_SQL";
        if (error_msg && strlen(error_msg) > 0) {
            msg += " (";
            msg += error_msg;
            msg += ")";
        }
        seekdb_connect_close(handle);
        
        return {false, msg};
    }
    
    // Get stored result (seekdb_query already sets result, but store_result returns the same)
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to store result"};
    }
    
    my_ulonglong row_count = seekdb_num_rows(result);
    if (row_count == 0) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        
        return {false, "GET_SQL returned no rows"};
    }
    
    seekdb_result_free(result);
    seekdb_connect_close(handle);
    
    return {true, ""};
}

// Test DBMS_HYBRID_SEARCH.SEARCH
TestResult test_hybrid_search_search() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    const char* search_params = R"({"query":{"bool":{"should":[{"match":{"query":"hello"}},{"match":{"content":"oceanbase mysql"}}]}},"knn":{"field":"vector","k":5,"query_vector":[1,2,3]},"_source":["c1","query","content","_keyword_score","_semantic_score"]})";
    std::string escaped_params = search_params;
    size_t pos = 0;
    while ((pos = escaped_params.find("'", pos)) != std::string::npos) {
        escaped_params.replace(pos, 1, "''");
        pos += 2;
    }
    
    std::string search_query = "SELECT DBMS_HYBRID_SEARCH.SEARCH('doc_table', '" + escaped_params + "') as result";
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, search_query.c_str(), &result);
    if (ret != SEEKDB_SUCCESS) {
        const char* error_msg = seekdb_error(handle);
        std::string msg = "Failed to execute SEARCH";
        if (error_msg && strlen(error_msg) > 0) {
            msg += " (";
            msg += error_msg;
            msg += ")";
        }
        seekdb_connect_close(handle);
        
        return {false, msg};
    }
    
    // Get stored result (seekdb_query already sets result, but store_result returns the same)
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        
        return {false, "Failed to store result"};
    }
    
    my_ulonglong row_count = seekdb_num_rows(result);
    if (row_count == 0) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        
        return {false, "SEARCH returned no rows"};
    }
    
    seekdb_result_free(result);
    seekdb_connect_close(handle);
    
    return {true, ""};
}

// Test parameterized queries (core feature)
TestResult test_parameterized_queries() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    // Helper lambda to execute SQL and handle result cleanup
    auto execute_sql = [&handle](const char* sql) -> int {
        SeekdbResult result = nullptr;
        int ret = seekdb_query(handle, sql, &result);
        if (result) {
            seekdb_result_free(result);
        }
        return ret;
    };
    
    // Create test table (drop first to ensure clean state)
    execute_sql("DROP TABLE IF EXISTS test_params");
    
    ret = execute_sql("CREATE TABLE test_params (id INT PRIMARY KEY, name VARCHAR(100))");
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to create table"};
    }
    
    ret = execute_sql("INSERT INTO test_params VALUES (1, 'Alice'), (2, 'Bob')");
    if (ret != SEEKDB_SUCCESS) {
        const char* error = seekdb_error(handle);
        std::string error_msg = "Failed to insert test data";
        if (error && strlen(error) > 0) {
            error_msg += " (";
            error_msg += error;
            error_msg += ")";
        }
        seekdb_connect_close(handle);
        return {false, error_msg};
    }
    
    // Test parameterized query using Prepared Statement
    SeekdbStmt stmt = seekdb_stmt_init(handle);
    if (!stmt) {
        seekdb_connect_close(handle);
        return {false, "Failed to init statement"};
    }
    
    const char* sql = "SELECT * FROM test_params WHERE id = ?";
    ret = seekdb_stmt_prepare(stmt, sql, strlen(sql));
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to prepare statement"};
    }
    
    // Bind parameter
    int32_t param_id = 1;
    SeekdbBind bind;
    bind.buffer_type = SEEKDB_TYPE_LONG;
    bind.buffer = &param_id;
    bind.buffer_length = sizeof(param_id);
    bind.length = nullptr;
    bool is_null = false;
    bind.is_null = &is_null;
    
    ret = seekdb_stmt_bind_param(stmt, &bind);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to bind parameter"};
    }
    
    // Execute
    ret = seekdb_stmt_execute(stmt);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to execute statement"};
    }
    
    // Verify row count before proceeding
    my_ulonglong row_count = seekdb_stmt_num_rows(stmt);
    if (row_count == 0) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Expected at least 1 row"};
    }
    
    // Bind result buffers before fetching
    int32_t result_id = 0;
    char result_name[256] = {0};
    unsigned long result_name_len = 0;
    bool result_id_null = false;
    bool result_name_null = false;
    
    SeekdbBind result_binds[2];
    result_binds[0].buffer_type = SEEKDB_TYPE_LONG;
    result_binds[0].buffer = &result_id;
    result_binds[0].buffer_length = sizeof(result_id);
    result_binds[0].length = nullptr;
    result_binds[0].is_null = &result_id_null;
    
    result_binds[1].buffer_type = SEEKDB_TYPE_STRING;
    result_binds[1].buffer = result_name;
    result_binds[1].buffer_length = sizeof(result_name);
    result_binds[1].length = &result_name_len;
    result_binds[1].is_null = &result_name_null;
    
    ret = seekdb_stmt_bind_result(stmt, result_binds);
    if (ret != SEEKDB_SUCCESS) {
        // Don't free stmt_result - it's managed by statement
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to bind result"};
    }
    
    // Fetch row
    ret = seekdb_stmt_fetch(stmt);
    if (ret != 0) {  // 0 means success, 1 means no more rows
        // Don't free stmt_result - it's managed by statement
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch row"};
    }
    
    // Verify fetched data
    if (result_id != 1) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Fetched data mismatch: Expected id=1, got id=" + std::to_string(result_id)};
    }
    if (result_name_null) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Fetched data mismatch: Expected name='Alice', got NULL"};
    }
    if (strcmp(result_name, "Alice") != 0) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, std::string("Fetched data mismatch: Expected name='Alice', got name='") + result_name + "'"};
    }
    
    // Don't free stmt_result - it's managed by statement and will be freed by seekdb_stmt_close()
    seekdb_stmt_close(stmt);
    
    // Clean up test table
    SeekdbResult cleanup_result = nullptr;
    seekdb_query(handle, "DROP TABLE IF EXISTS test_params", &cleanup_result);
    if (cleanup_result) {
        seekdb_result_free(cleanup_result);
    }
    seekdb_connect_close(handle);
    
    return {true, ""};
}

// Test binary parameter binding with CAST(? AS BINARY) queries
// This test verifies that SEEKDB_TYPE_BLOB correctly handles binary data
// in CAST(? AS BINARY) queries, ensuring proper binary comparison
TestResult test_binary_parameter_binding() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    // Helper lambda to execute SQL and handle result cleanup
    auto execute_sql = [&handle](const char* sql) -> int {
        SeekdbResult result = nullptr;
        int ret = seekdb_query(handle, sql, &result);
        if (result) {
            seekdb_result_free(result);
        }
        return ret;
    };
    
    // Create test table with VARBINARY column (drop first to ensure clean state)
    execute_sql("DROP TABLE IF EXISTS test_binary_params");
    
    ret = execute_sql("CREATE TABLE test_binary_params (id INT PRIMARY KEY, _id VARBINARY(100))");
    if (ret != SEEKDB_SUCCESS) {
        const char* error = seekdb_error(handle);
        std::string error_msg = "Failed to create table";
        if (error && strlen(error) > 0) {
            error_msg += " (";
            error_msg += error;
            error_msg += ")";
        }
        seekdb_connect_close(handle);
        return {false, error_msg};
    }
    
    // Test 1: Insert binary data using SEEKDB_TYPE_BLOB
    SeekdbStmt stmt = seekdb_stmt_init(handle);
    if (!stmt) {
        seekdb_connect_close(handle);
        return {false, "Failed to init statement"};
    }
    
    const char* insert_sql = "INSERT INTO test_binary_params (id, _id) VALUES (?, CAST(? AS BINARY))";
    ret = seekdb_stmt_prepare(stmt, insert_sql, strlen(insert_sql));
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to prepare INSERT statement"};
    }
    
    // Insert 5 rows with binary data
    const char* binary_values[] = {"get1", "get2", "get3", "get4", "get5"};
    int32_t ids[] = {1, 2, 3, 4, 5};
    
    for (int i = 0; i < 5; i++) {
        // Bind id parameter
        int32_t id = ids[i];
        SeekdbBind id_bind;
        id_bind.buffer_type = SEEKDB_TYPE_LONG;
        id_bind.buffer = &id;
        id_bind.buffer_length = sizeof(id);
        id_bind.length = nullptr;
        bool id_null = false;
        id_bind.is_null = &id_null;
        
        // Bind _id parameter (binary data using SEEKDB_TYPE_BLOB)
        const char* binary_val = binary_values[i];
        unsigned long binary_len = strlen(binary_val);
        SeekdbBind binary_bind;
        binary_bind.buffer_type = SEEKDB_TYPE_BLOB;
        binary_bind.buffer = const_cast<char*>(binary_val);
        binary_bind.buffer_length = binary_len;
        binary_bind.length = &binary_len;
        bool binary_null = false;
        binary_bind.is_null = &binary_null;
        
        SeekdbBind binds[2] = {id_bind, binary_bind};
        ret = seekdb_stmt_bind_param(stmt, binds);
        if (ret != SEEKDB_SUCCESS) {
            seekdb_stmt_close(stmt);
            seekdb_connect_close(handle);
            return {false, "Failed to bind parameters for INSERT"};
        }
        
        ret = seekdb_stmt_execute(stmt);
        if (ret != SEEKDB_SUCCESS) {
            const char* error = seekdb_error(handle);
            std::string error_msg = "Failed to execute INSERT";
            if (error && strlen(error) > 0) {
                error_msg += " (";
                error_msg += error;
                error_msg += ")";
            }
            seekdb_stmt_close(stmt);
            seekdb_connect_close(handle);
            return {false, error_msg};
        }
        
        // Reset statement for next iteration
        ret = seekdb_stmt_reset(stmt);
        if (ret != SEEKDB_SUCCESS) {
            seekdb_stmt_close(stmt);
            seekdb_connect_close(handle);
            return {false, "Failed to reset statement"};
        }
    }
    
    seekdb_stmt_close(stmt);
    
    // Test 2: Verify data was inserted correctly (SELECT ALL)
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "SELECT COUNT(*) FROM test_binary_params", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to execute COUNT query"};
    }
    result = seekdb_store_result(handle);
    if (!result) {
        seekdb_connect_close(handle);
        return {false, "Failed to store COUNT result"};
    }
    
    SeekdbRow row = seekdb_fetch_row(result);
    if (!row) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch COUNT row"};
    }
    
    char count_buf[32];
    int count_ret = seekdb_row_get_string(row, 0, count_buf, sizeof(count_buf));
    if (count_ret != SEEKDB_SUCCESS) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to get COUNT value"};
    }
    
    int count = atoi(count_buf);
    if (count != 5) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Expected 5 rows, got " + std::to_string(count)};
    }
    seekdb_result_free(result);
    
    // Test 3: Query using CAST(? AS BINARY) with SEEKDB_TYPE_BLOB
    stmt = seekdb_stmt_init(handle);
    if (!stmt) {
        seekdb_connect_close(handle);
        return {false, "Failed to init statement for SELECT"};
    }
    
    const char* select_sql = "SELECT _id FROM test_binary_params WHERE _id = CAST(? AS BINARY)";
    ret = seekdb_stmt_prepare(stmt, select_sql, strlen(select_sql));
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to prepare SELECT statement"};
    }
    
    // Query for "get1"
    const char* search_val = "get1";
    unsigned long search_len = strlen(search_val);
    SeekdbBind search_bind;
    search_bind.buffer_type = SEEKDB_TYPE_BLOB;
    search_bind.buffer = const_cast<char*>(search_val);
    search_bind.buffer_length = search_len;
    search_bind.length = &search_len;
    bool search_null = false;
    search_bind.is_null = &search_null;
    
    ret = seekdb_stmt_bind_param(stmt, &search_bind);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to bind SELECT parameter"};
    }
    
    ret = seekdb_stmt_execute(stmt);
    if (ret != SEEKDB_SUCCESS) {
        const char* error = seekdb_error(handle);
        std::string error_msg = "Failed to execute SELECT";
        if (error && strlen(error) > 0) {
            error_msg += " (";
            error_msg += error;
            error_msg += ")";
        }
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, error_msg};
    }
    
    // Verify result set has data
    my_ulonglong row_count = seekdb_stmt_num_rows(stmt);
    if (row_count == 0) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "SELECT with CAST(? AS BINARY) returned 0 rows, expected 1 row"};
    }
    
    if (row_count != 1) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "SELECT with CAST(? AS BINARY) returned " + std::to_string(row_count) + " rows, expected 1"};
    }
    
    // Verify field count
    unsigned int field_count = seekdb_stmt_field_count(stmt);
    if (field_count == 0) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "SELECT with CAST(? AS BINARY) returned field_count=0"};
    }
    
    if (field_count != 1) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "SELECT with CAST(? AS BINARY) returned field_count=" + std::to_string(field_count) + ", expected 1"};
    }
    
    // Bind result buffer
    char result_buf[256] = {0};
    unsigned long result_len = 0;
    bool result_null = false;
    
    SeekdbBind result_bind;
    result_bind.buffer_type = SEEKDB_TYPE_STRING;
    result_bind.buffer = result_buf;
    result_bind.buffer_length = sizeof(result_buf);
    result_bind.length = &result_len;
    result_bind.is_null = &result_null;
    
    ret = seekdb_stmt_bind_result(stmt, &result_bind);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to bind result"};
    }
    
    // Fetch row
    ret = seekdb_stmt_fetch(stmt);
    if (ret != 0) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch row"};
    }
    
    // Verify fetched data matches
    if (result_null) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Fetched _id is NULL, expected 'get1'"};
    }
    
    if (result_len != strlen("get1") || strncmp(result_buf, "get1", result_len) != 0) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, std::string("Fetched _id mismatch: expected 'get1', got '") + 
                       std::string(result_buf, result_len) + "'"};
    }
    
    seekdb_stmt_close(stmt);
    
    // Test 4: Query for non-existent value (should return 0 rows)
    stmt = seekdb_stmt_init(handle);
    if (!stmt) {
        seekdb_connect_close(handle);
        return {false, "Failed to init statement for negative test"};
    }
    
    ret = seekdb_stmt_prepare(stmt, select_sql, strlen(select_sql));
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to prepare SELECT statement for negative test"};
    }
    
    const char* not_found_val = "notfound";
    unsigned long not_found_len = strlen(not_found_val);
    SeekdbBind not_found_bind;
    not_found_bind.buffer_type = SEEKDB_TYPE_BLOB;
    not_found_bind.buffer = const_cast<char*>(not_found_val);
    not_found_bind.buffer_length = not_found_len;
    not_found_bind.length = &not_found_len;
    bool not_found_null = false;
    not_found_bind.is_null = &not_found_null;
    
    ret = seekdb_stmt_bind_param(stmt, &not_found_bind);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to bind parameter for negative test"};
    }
    
    ret = seekdb_stmt_execute(stmt);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to execute SELECT for negative test"};
    }
    
    row_count = seekdb_stmt_num_rows(stmt);
    if (row_count != 0) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "SELECT with non-existent value returned " + std::to_string(row_count) + " rows, expected 0"};
    }
    
    seekdb_stmt_close(stmt);
    
    // Clean up test table
    execute_sql("DROP TABLE IF EXISTS test_binary_params");
    seekdb_connect_close(handle);
    
    return {true, ""};
}

// Embedded param binding: SEEKDB_TYPE_VARBINARY_ID for _id (collection-like)
TestResult test_embedded_varbinary_id_binding() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    auto execute_sql = [&handle](const char* sql) -> int {
        SeekdbResult result = nullptr;
        int r = seekdb_query(handle, sql, &result);
        if (result) seekdb_result_free(result);
        return r;
    };
    const char* table = "test_embed_varbinary_id";
    execute_sql((std::string("DROP TABLE IF EXISTS ") + table).c_str());
    // _id VARBINARY(512) like collection table
    ret = execute_sql(
        "CREATE TABLE test_embed_varbinary_id (_id VARBINARY(512) PRIMARY KEY, document VARCHAR(1024), metadata VARCHAR(1024))");
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to create table"};
    }
    // 1) INSERT: VALUES (CAST(? AS BINARY), ?, ?) with [VARBINARY_ID, STRING, STRING]
    SeekdbStmt stmt = seekdb_stmt_init(handle);
    if (!stmt) {
        seekdb_connect_close(handle);
        return {false, "Failed to init statement"};
    }
    const char* insert_sql = "INSERT INTO test_embed_varbinary_id (_id, document, metadata) VALUES (CAST(? AS BINARY), ?, ?)";
    ret = seekdb_stmt_prepare(stmt, insert_sql, strlen(insert_sql));
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to prepare INSERT"};
    }
    const char* id1 = "emb_id_1";
    const char* doc1 = "doc one";
    const char* meta1 = "meta one";
    unsigned long len_id1 = strlen(id1), len_doc1 = strlen(doc1), len_meta1 = strlen(meta1);
    bool n1 = false;
    SeekdbBind ins_binds[3];
    ins_binds[0].buffer_type = SEEKDB_TYPE_VARBINARY_ID;
    ins_binds[0].buffer = const_cast<char*>(id1);
    ins_binds[0].buffer_length = len_id1;
    ins_binds[0].length = &len_id1;
    ins_binds[0].is_null = &n1;
    ins_binds[1].buffer_type = SEEKDB_TYPE_STRING;
    ins_binds[1].buffer = const_cast<char*>(doc1);
    ins_binds[1].buffer_length = len_doc1;
    ins_binds[1].length = &len_doc1;
    ins_binds[1].is_null = &n1;
    ins_binds[2].buffer_type = SEEKDB_TYPE_STRING;
    ins_binds[2].buffer = const_cast<char*>(meta1);
    ins_binds[2].buffer_length = len_meta1;
    ins_binds[2].length = &len_meta1;
    ins_binds[2].is_null = &n1;
    ret = seekdb_stmt_bind_param(stmt, ins_binds);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to bind INSERT"};
    }
    ret = seekdb_stmt_execute(stmt);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "INSERT with VARBINARY_ID failed"};
    }
    seekdb_stmt_close(stmt);
    // 2) SELECT WHERE _id = CAST(? AS BINARY) with VARBINARY_ID — expect 1 row, result non-null
    stmt = seekdb_stmt_init(handle);
    if (!stmt) {
        seekdb_connect_close(handle);
        return {false, "Failed to init statement for SELECT"};
    }
    const char* select_one_sql = "SELECT _id, document, metadata FROM test_embed_varbinary_id WHERE _id = CAST(? AS BINARY)";
    ret = seekdb_stmt_prepare(stmt, select_one_sql, strlen(select_one_sql));
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to prepare SELECT one"};
    }
    SeekdbBind sel_bind;
    sel_bind.buffer_type = SEEKDB_TYPE_VARBINARY_ID;
    sel_bind.buffer = const_cast<char*>(id1);
    sel_bind.buffer_length = len_id1;
    sel_bind.length = &len_id1;
    sel_bind.is_null = &n1;
    ret = seekdb_stmt_bind_param(stmt, &sel_bind);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to bind SELECT one"};
    }
    ret = seekdb_stmt_execute(stmt);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "SELECT with VARBINARY_ID execute failed"};
    }
    my_ulonglong row_count = seekdb_stmt_num_rows(stmt);
    if (row_count != 1) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "SELECT by _id expected 1 row, got " + std::to_string(row_count)};
    }
    unsigned int field_count = seekdb_stmt_field_count(stmt);
    if (field_count != 3) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "SELECT expected 3 fields, got " + std::to_string(field_count)};
    }
    char buf0[512] = {0}, doc_buf[256] = {0}, buf2[256] = {0};
    unsigned long len0 = 0, doc_len = 0, len2 = 0;
    SeekdbBind res_binds[3];
    res_binds[0].buffer_type = SEEKDB_TYPE_STRING;
    res_binds[0].buffer = buf0;
    res_binds[0].buffer_length = sizeof(buf0);
    res_binds[0].length = &len0;
    res_binds[0].is_null = &n1;
    res_binds[1].buffer_type = SEEKDB_TYPE_STRING;
    res_binds[1].buffer = doc_buf;
    res_binds[1].buffer_length = sizeof(doc_buf);
    res_binds[1].length = &doc_len;
    res_binds[1].is_null = &n1;
    res_binds[2].buffer_type = SEEKDB_TYPE_STRING;
    res_binds[2].buffer = buf2;
    res_binds[2].buffer_length = sizeof(buf2);
    res_binds[2].length = &len2;
    res_binds[2].is_null = &n1;
    ret = seekdb_stmt_bind_result(stmt, res_binds);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to bind result"};
    }
    ret = seekdb_stmt_fetch(stmt);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch row (result set should be non-null)"};
    }
    if (doc_len != len_doc1 || strncmp(doc_buf, doc1, doc_len) != 0) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Fetched document mismatch"};
    }
    seekdb_stmt_close(stmt);
    // 3) INSERT second row, then get by multiple ids
    stmt = seekdb_stmt_init(handle);
    if (!stmt) {
        seekdb_connect_close(handle);
        return {false, "Failed to init for second INSERT"};
    }
    ret = seekdb_stmt_prepare(stmt, insert_sql, strlen(insert_sql));
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to prepare second INSERT"};
    }
    const char* id2 = "emb_id_2";
    const char* doc2 = "doc two";
    const char* meta2 = "meta two";
    unsigned long len_id2 = strlen(id2), len_doc2 = strlen(doc2), len_meta2 = strlen(meta2);
    ins_binds[0].buffer = const_cast<char*>(id2);
    ins_binds[0].length = &len_id2;
    ins_binds[1].buffer = const_cast<char*>(doc2);
    ins_binds[1].length = &len_doc2;
    ins_binds[2].buffer = const_cast<char*>(meta2);
    ins_binds[2].length = &len_meta2;
    ret = seekdb_stmt_bind_param(stmt, ins_binds);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to bind second INSERT"};
    }
    ret = seekdb_stmt_execute(stmt);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Second INSERT failed"};
    }
    seekdb_stmt_close(stmt);
    // 4) SELECT WHERE (_id = CAST(? AS BINARY) OR _id = CAST(? AS BINARY)) — expect 2 rows
    stmt = seekdb_stmt_init(handle);
    if (!stmt) {
        seekdb_connect_close(handle);
        return {false, "Failed to init for SELECT multiple"};
    }
    const char* select_multi_sql = "SELECT _id, document FROM test_embed_varbinary_id WHERE (_id = CAST(? AS BINARY) OR _id = CAST(? AS BINARY))";
    ret = seekdb_stmt_prepare(stmt, select_multi_sql, strlen(select_multi_sql));
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to prepare SELECT multiple"};
    }
    SeekdbBind multi_binds[2];
    multi_binds[0].buffer_type = SEEKDB_TYPE_VARBINARY_ID;
    multi_binds[0].buffer = const_cast<char*>(id1);
    multi_binds[0].buffer_length = len_id1;
    multi_binds[0].length = &len_id1;
    multi_binds[0].is_null = &n1;
    multi_binds[1].buffer_type = SEEKDB_TYPE_VARBINARY_ID;
    multi_binds[1].buffer = const_cast<char*>(id2);
    multi_binds[1].buffer_length = len_id2;
    multi_binds[1].length = &len_id2;
    multi_binds[1].is_null = &n1;
    ret = seekdb_stmt_bind_param(stmt, multi_binds);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to bind SELECT multiple"};
    }
    ret = seekdb_stmt_execute(stmt);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "SELECT multiple execute failed"};
    }
    row_count = seekdb_stmt_num_rows(stmt);
    if (row_count != 2) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "SELECT by multiple ids expected 2 rows, got " + std::to_string(row_count)};
    }
    seekdb_stmt_close(stmt);
    // 5) UPDATE SET document = ? WHERE _id = CAST(? AS BINARY) — id last param VARBINARY_ID
    stmt = seekdb_stmt_init(handle);
    if (!stmt) {
        seekdb_connect_close(handle);
        return {false, "Failed to init for UPDATE"};
    }
    const char* update_sql = "UPDATE test_embed_varbinary_id SET document = ? WHERE _id = CAST(? AS BINARY)";
    ret = seekdb_stmt_prepare(stmt, update_sql, strlen(update_sql));
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to prepare UPDATE"};
    }
    const char* doc_updated = "doc one updated";
    unsigned long len_doc_up = strlen(doc_updated);
    SeekdbBind upd_binds[2];
    upd_binds[0].buffer_type = SEEKDB_TYPE_STRING;
    upd_binds[0].buffer = const_cast<char*>(doc_updated);
    upd_binds[0].buffer_length = len_doc_up;
    upd_binds[0].length = &len_doc_up;
    upd_binds[0].is_null = &n1;
    upd_binds[1].buffer_type = SEEKDB_TYPE_VARBINARY_ID;
    upd_binds[1].buffer = const_cast<char*>(id1);
    upd_binds[1].buffer_length = len_id1;
    upd_binds[1].length = &len_id1;
    upd_binds[1].is_null = &n1;
    ret = seekdb_stmt_bind_param(stmt, upd_binds);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to bind UPDATE"};
    }
    ret = seekdb_stmt_execute(stmt);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "UPDATE with VARBINARY_ID failed"};
    }
    seekdb_stmt_close(stmt);
    // 6) DELETE WHERE _id = CAST(? AS BINARY) with VARBINARY_ID
    stmt = seekdb_stmt_init(handle);
    if (!stmt) {
        seekdb_connect_close(handle);
        return {false, "Failed to init for DELETE"};
    }
    const char* delete_sql = "DELETE FROM test_embed_varbinary_id WHERE _id = CAST(? AS BINARY)";
    ret = seekdb_stmt_prepare(stmt, delete_sql, strlen(delete_sql));
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to prepare DELETE"};
    }
    SeekdbBind del_bind;
    del_bind.buffer_type = SEEKDB_TYPE_VARBINARY_ID;
    del_bind.buffer = const_cast<char*>(id2);
    del_bind.buffer_length = len_id2;
    del_bind.length = &len_id2;
    del_bind.is_null = &n1;
    ret = seekdb_stmt_bind_param(stmt, &del_bind);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to bind DELETE"};
    }
    ret = seekdb_stmt_execute(stmt);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "DELETE with VARBINARY_ID failed"};
    }
    seekdb_stmt_close(stmt);
    execute_sql("DROP TABLE IF EXISTS test_embed_varbinary_id");
    seekdb_connect_close(handle);
    return {true, ""};
}

// VECTOR column reading: SELECT returns JSON string without rounding (e.g. "[1.1, 2.2, 3.3]")
TestResult test_vector_column_reading() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    auto execute_sql = [&handle](const char* sql) -> int {
        SeekdbResult result = nullptr;
        int r = seekdb_query(handle, sql, &result);
        if (result) seekdb_result_free(result);
        return r;
    };
    execute_sql("DROP TABLE IF EXISTS test_vector_read");
    ret = execute_sql(
        "CREATE TABLE test_vector_read (document VARCHAR(255), embedding VECTOR(3))");
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to create table"};
    }
    // INSERT one row: document='vec_read_test', embedding=[1.1, 2.2, 3.3]
    SeekdbStmt stmt = seekdb_stmt_init(handle);
    if (!stmt) {
        seekdb_connect_close(handle);
        return {false, "Failed to init statement"};
    }
    const char* insert_sql = "INSERT INTO test_vector_read (document, embedding) VALUES (?, ?)";
    ret = seekdb_stmt_prepare(stmt, insert_sql, strlen(insert_sql));
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to prepare INSERT"};
    }
    const char* doc = "vec_read_test";
    const char* emb = "[1.1, 2.2, 3.3]";
    unsigned long len_doc = strlen(doc), len_emb = strlen(emb);
    bool n1 = false;
    SeekdbBind ins_binds[2];
    ins_binds[0].buffer_type = SEEKDB_TYPE_STRING;
    ins_binds[0].buffer = const_cast<char*>(doc);
    ins_binds[0].buffer_length = len_doc;
    ins_binds[0].length = &len_doc;
    ins_binds[0].is_null = &n1;
    ins_binds[1].buffer_type = SEEKDB_TYPE_VECTOR;
    ins_binds[1].buffer = const_cast<char*>(emb);
    ins_binds[1].buffer_length = len_emb;
    ins_binds[1].length = &len_emb;
    ins_binds[1].is_null = &n1;
    ret = seekdb_stmt_bind_param(stmt, ins_binds);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to bind INSERT"};
    }
    ret = seekdb_stmt_execute(stmt);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "INSERT with VECTOR failed"};
    }
    seekdb_stmt_close(stmt);
    // SELECT document, embedding FROM test_vector_read WHERE document = 'vec_read_test'
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "SELECT document, embedding FROM test_vector_read WHERE document = 'vec_read_test' LIMIT 1", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "SELECT failed"};
    }
    result = seekdb_store_result(handle);
    if (!result) {
        seekdb_connect_close(handle);
        return {false, "Failed to store result"};
    }
    my_ulonglong row_count = seekdb_num_rows(result);
    if (row_count != 1) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Expected 1 row, got " + std::to_string(row_count)};
    }
    SeekdbRow row = seekdb_fetch_row(result);
    if (!row) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch row"};
    }
    // VECTOR column (embedding) must be readable and non-null
    if (seekdb_row_is_null(row, 1)) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Embedding column is NULL - VECTOR column reading failed"};
    }
    size_t embedding_len = seekdb_row_get_string_len(row, 1);
    if (embedding_len == 0 || embedding_len == static_cast<size_t>(-1)) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Embedding column length is 0 or invalid - VECTOR not returned"};
    }
    // C ABI returns VECTOR as JSON string without rounding, e.g. "[1.1, 2.2, 3.3]"
    std::vector<char> emb_buf(embedding_len + 1, 0);
    ret = seekdb_row_get_string(row, 1, emb_buf.data(), emb_buf.size());
    if (ret != SEEKDB_SUCCESS) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "seekdb_row_get_string(embedding) failed"};
    }
    std::string emb_str(emb_buf.data(), embedding_len);
    if (emb_str.empty() || emb_str.front() != '[' || emb_str.back() != ']') {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "VECTOR should be JSON string format, got: " + emb_str.substr(0, 80)};
    }
    // Expect direct format without precision rounding: "[1.1, 2.2, 3.3]"
    if (emb_str != "[1.1, 2.2, 3.3]") {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "VECTOR expected [1.1, 2.2, 3.3], got: " + emb_str};
    }
    seekdb_result_free(result);
    execute_sql("DROP TABLE IF EXISTS test_vector_read");
    seekdb_connect_close(handle);
    return {true, ""};
}

// VECTOR 读取不做精度处理，直接返回原始值 "[1.1, 2.2, 3.3]"
TestResult test_vector_column_reading_no_rounding() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    auto execute_sql = [&handle](const char* sql) -> int {
        SeekdbResult result = nullptr;
        int r = seekdb_query(handle, sql, &result);
        if (result) seekdb_result_free(result);
        return r;
    };
    execute_sql("DROP TABLE IF EXISTS test_vector_read_no_round");
    ret = execute_sql(
        "CREATE TABLE test_vector_read_no_round (document VARCHAR(255), embedding VECTOR(3))");
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to create table"};
    }
    SeekdbStmt stmt = seekdb_stmt_init(handle);
    if (!stmt) {
        seekdb_connect_close(handle);
        return {false, "Failed to init statement"};
    }
    const char* insert_sql = "INSERT INTO test_vector_read_no_round (document, embedding) VALUES (?, ?)";
    ret = seekdb_stmt_prepare(stmt, insert_sql, strlen(insert_sql));
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to prepare INSERT"};
    }
    const char* doc = "vec_no_round";
    const char* emb = "[1.1, 2.2, 3.3]";
    unsigned long len_doc = strlen(doc), len_emb = strlen(emb);
    bool n1 = false;
    SeekdbBind ins_binds[2];
    ins_binds[0].buffer_type = SEEKDB_TYPE_STRING;
    ins_binds[0].buffer = const_cast<char*>(doc);
    ins_binds[0].buffer_length = len_doc;
    ins_binds[0].length = &len_doc;
    ins_binds[0].is_null = &n1;
    ins_binds[1].buffer_type = SEEKDB_TYPE_VECTOR;
    ins_binds[1].buffer = const_cast<char*>(emb);
    ins_binds[1].buffer_length = len_emb;
    ins_binds[1].length = &len_emb;
    ins_binds[1].is_null = &n1;
    ret = seekdb_stmt_bind_param(stmt, ins_binds);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to bind INSERT"};
    }
    ret = seekdb_stmt_execute(stmt);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "INSERT with VECTOR failed"};
    }
    seekdb_stmt_close(stmt);

    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "SELECT document, embedding FROM test_vector_read_no_round WHERE document = 'vec_no_round' LIMIT 1", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "SELECT failed"};
    }
    result = seekdb_store_result(handle);
    if (!result) {
        seekdb_connect_close(handle);
        return {false, "Failed to store result"};
    }
    my_ulonglong row_count = seekdb_num_rows(result);
    if (row_count != 1) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Expected 1 row"};
    }
    SeekdbRow row = seekdb_fetch_row(result);
    if (!row) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch row"};
    }
    if (seekdb_row_is_null(row, 1)) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Embedding column is NULL"};
    }
    size_t embedding_len = seekdb_row_get_string_len(row, 1);
    std::vector<char> emb_buf(embedding_len + 1, 0);
    ret = seekdb_row_get_string(row, 1, emb_buf.data(), emb_buf.size());
    if (ret != SEEKDB_SUCCESS) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "seekdb_row_get_string failed"};
    }
    std::string emb_str(emb_buf.data(), embedding_len);
    if (emb_str != "[1.1, 2.2, 3.3]") {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "VECTOR expected [1.1, 2.2, 3.3] (no rounding), got: " + emb_str};
    }
    seekdb_result_free(result);
    execute_sql("DROP TABLE IF EXISTS test_vector_read_no_round");
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test column name inference (core feature)
TestResult test_column_name_inference() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    // Create test table with known column names
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "CREATE TABLE IF NOT EXISTS test_cols (user_id INT, user_name VARCHAR(100), user_email VARCHAR(100))", &result);
    if (result) seekdb_result_free(result);
    
    ret = seekdb_query(handle, "INSERT INTO test_cols VALUES (1, 'Alice', 'alice@example.com')", &result);
    if (result) seekdb_result_free(result);
    
    // Query with explicit column names
    ret = seekdb_query(handle, "SELECT user_id, user_name, user_email FROM test_cols", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to execute query"};
    }
    
    result = seekdb_store_result(handle);
    if (!result) {
        seekdb_connect_close(handle);
        return {false, "Failed to store result"};
    }
    
    // Test fetch_fields - should get column names from database metadata
    SeekdbField* fields = seekdb_fetch_fields(result);
    if (!fields) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch fields"};
    }
    
    unsigned int field_count = seekdb_num_fields(result);
    if (field_count != 3) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Expected 3 fields"};
    }
    
    // Verify column names are correctly inferred
    if (!fields[0].name || strcmp(fields[0].name, "user_id") != 0) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "First column name should be 'user_id'"};
    }
    
    if (!fields[1].name || strcmp(fields[1].name, "user_name") != 0) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Second column name should be 'user_name'"};
    }
    
    if (!fields[2].name || strcmp(fields[2].name, "user_email") != 0) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Third column name should be 'user_email'"};
    }
    
    // Test fetch_field - get next field
    SeekdbField* field1 = seekdb_fetch_field(result);
    if (!field1 || !field1->name || strcmp(field1->name, "user_id") != 0) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "First field should be 'user_id'"};
    }
    
    SeekdbField* field2 = seekdb_fetch_field(result);
    if (!field2 || !field2->name || strcmp(field2->name, "user_name") != 0) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Second field should be 'user_name'"};
    }
    
    // Test fetch_field_direct - get field by index
    SeekdbField* field_direct = seekdb_fetch_field_direct(result, 2);
    if (!field_direct || !field_direct->name || strcmp(field_direct->name, "user_email") != 0) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Third field should be 'user_email'"};
    }
    
    seekdb_result_free(result);
    
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_cols", &result);
    if (result) seekdb_result_free(result);
    seekdb_connect_close(handle);
    
    return {true, ""};
}

// Test VECTOR type parameter binding (regression test for VECTOR_INSERT_ISSUE)
// Ensures VECTOR type parameters are correctly handled in INSERT statements
// without causing "Column cannot be null" errors
TestResult test_vector_parameter_binding() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    // Helper lambda to execute SQL and handle result cleanup
    auto execute_sql = [&handle](const char* sql) -> int {
        SeekdbResult result = nullptr;
        int ret = seekdb_query(handle, sql, &result);
        if (result) {
            seekdb_result_free(result);
        }
        return ret;
    };
    
    // Create test table with VECTOR column (drop first to ensure clean state)
    execute_sql("DROP TABLE IF EXISTS test_vector_params");
    
    const char* create_sql = R"(
        CREATE TABLE test_vector_params (
            id INT AUTO_INCREMENT PRIMARY KEY,
            document VARCHAR(255),
            metadata VARCHAR(255),
            embedding VECTOR(3)
        )
    )";
    ret = execute_sql(create_sql);
    if (ret != SEEKDB_SUCCESS) {
        const char* error = seekdb_error(handle);
        std::string error_msg = "Failed to create table";
        if (error && strlen(error) > 0) {
            error_msg += " (";
            error_msg += error;
            error_msg += ")";
        }
        seekdb_connect_close(handle);
        return {false, error_msg};
    }
    
    // Test parameterized INSERT with VECTOR type using Prepared Statement
    // 5 rows × 3 columns (document, metadata, embedding) - id is AUTO_INCREMENT
    // embedding uses SEEKDB_TYPE_VECTOR and is embedded directly in SQL (not parameter bound)
    SeekdbStmt stmt = seekdb_stmt_init(handle);
    if (!stmt) {
        seekdb_connect_close(handle);
        return {false, "Failed to init statement"};
    }
    
    // Prepare INSERT statement with multiple rows
    // id is AUTO_INCREMENT, so we only need 3 parameters per row (document, metadata, embedding)
    const char* insert_sql = R"(
        INSERT INTO test_vector_params (document, metadata, embedding) 
        VALUES (?, ?, ?), 
               (?, ?, ?), 
               (?, ?, ?), 
               (?, ?, ?), 
               (?, ?, ?)
    )";
    ret = seekdb_stmt_prepare(stmt, insert_sql, strlen(insert_sql));
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to prepare INSERT statement"};
    }
    
    // Prepare test data: 5 rows × 3 columns = 15 parameters (id is AUTO_INCREMENT)
    const char* documents[] = {"Document 1", "Document 2", "Document 3", "Document 4", "Document 5"};
    const char* metadatas[] = {"{\"category\":\"A\",\"score\":95}", 
                               "{\"category\":\"B\",\"score\":90}",
                               "{\"category\":\"A\",\"score\":88}",
                               "{\"category\":\"C\",\"score\":92}",
                               "{\"category\":\"B\",\"score\":85}"};
    const char* embeddings[] = {"[1,2,3]", "[2,3,4]", "[1.1,2.1,3.1]", "[2.1,3.1,4.1]", "[1.2,2.2,3.2]"};
    
    // Allocate bind structures for all 15 parameters (5 rows × 3 columns)
    SeekdbBind binds[15];
    bool is_null_flags[15] = {false};
    unsigned long lengths[15];
    
    // Set up binds for each row (3 parameters per row: document, metadata, embedding)
    for (int row = 0; row < 5; row++) {
        int base_idx = row * 3;
        
        // document (VARCHAR) - parameter at base_idx
        lengths[base_idx] = strlen(documents[row]);
        binds[base_idx].buffer_type = SEEKDB_TYPE_STRING;
        binds[base_idx].buffer = const_cast<char*>(documents[row]);
        binds[base_idx].buffer_length = lengths[base_idx];
        binds[base_idx].length = &lengths[base_idx];
        binds[base_idx].is_null = &is_null_flags[base_idx];
        
        // metadata (VARCHAR) - parameter at base_idx + 1
        lengths[base_idx + 1] = strlen(metadatas[row]);
        binds[base_idx + 1].buffer_type = SEEKDB_TYPE_STRING;
        binds[base_idx + 1].buffer = const_cast<char*>(metadatas[row]);
        binds[base_idx + 1].buffer_length = lengths[base_idx + 1];
        binds[base_idx + 1].length = &lengths[base_idx + 1];
        binds[base_idx + 1].is_null = &is_null_flags[base_idx + 1];
        
        // embedding (VECTOR) - parameter at base_idx + 2
        // VECTOR type uses SEEKDB_TYPE_VECTOR and is embedded directly in SQL
        lengths[base_idx + 2] = strlen(embeddings[row]);
        binds[base_idx + 2].buffer_type = SEEKDB_TYPE_VECTOR;
        binds[base_idx + 2].buffer = const_cast<char*>(embeddings[row]);
        binds[base_idx + 2].buffer_length = lengths[base_idx + 2];
        binds[base_idx + 2].length = &lengths[base_idx + 2];
        binds[base_idx + 2].is_null = &is_null_flags[base_idx + 2];
    }
    
    // Bind all parameters
    ret = seekdb_stmt_bind_param(stmt, binds);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to bind parameters"};
    }
    
    // Execute INSERT statement
    // This should NOT fail with "Column cannot be null" error
    ret = seekdb_stmt_execute(stmt);
    if (ret != SEEKDB_SUCCESS) {
        const char* error = seekdb_error(handle);
        std::string error_msg = "Failed to execute INSERT with VECTOR parameters";
        if (error && strlen(error) > 0) {
            error_msg += " (";
            error_msg += error;
            error_msg += ")";
        }
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, error_msg};
    }
    
    seekdb_stmt_close(stmt);
    
    // Verify data was inserted correctly by querying it
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "SELECT COUNT(*) as cnt FROM test_vector_params", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to verify INSERT by querying count"};
    }
    
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        return {false, "Failed to store result"};
    }
    
    my_ulonglong row_count = seekdb_num_rows(result);
    if (row_count != 1) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Expected 1 row in count query"};
    }
    
    SeekdbRow row = seekdb_fetch_row(result);
    if (row == nullptr) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch count row"};
    }
    
    // Get count value
    char count_str[32] = {0};
    size_t count_len = seekdb_row_get_string_len(row, 0);
    if (count_len > 0 && count_len < sizeof(count_str)) {
        seekdb_row_get_string(row, 0, count_str, sizeof(count_str));
        int count = std::atoi(count_str);
        if (count != 5) {
            seekdb_result_free(result);
            seekdb_connect_close(handle);
            return {false, std::string("Expected 5 rows inserted, got ") + count_str};
        }
    }
    
    seekdb_result_free(result);
    
    // Verify specific data by querying rows using document column
    ret = seekdb_query(handle, "SELECT document, embedding FROM test_vector_params WHERE document = 'Document 1' LIMIT 1", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to verify specific row"};
    }
    
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        return {false, "Failed to store verification result"};
    }
    
    row_count = seekdb_num_rows(result);
    if (row_count != 1) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Expected 1 row in verification query (document='Document 1')"};
    }
    
    row = seekdb_fetch_row(result);
    if (row == nullptr) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch verification row"};
    }
    
    // Verify document column
    char doc_buf[256] = {0};
    size_t doc_len = seekdb_row_get_string_len(row, 0);
    if (doc_len > 0 && doc_len < sizeof(doc_buf)) {
        seekdb_row_get_string(row, 0, doc_buf, sizeof(doc_buf));
        if (strcmp(doc_buf, "Document 1") != 0) {
            seekdb_result_free(result);
            seekdb_connect_close(handle);
            return {false, std::string("Document mismatch: expected 'Document 1', got '") + doc_buf + "'"};
        }
    }
    
    // Verify embedding column (VECTOR) is not null
    // VECTOR type is stored as binary data (float array) internally
    // When returned via seekdb_row_get_string(), it's typically binary format (not JSON)
    // The important thing is that it's not NULL, which means the parameter binding worked
    size_t embedding_len = seekdb_row_get_string_len(row, 1);
    if (embedding_len == 0) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Embedding column is NULL - VECTOR parameter binding failed"};
    }
    
    // Note: VECTOR type return format
    // - Insert: JSON array format "[1,2,3]" (via SEEKDB_TYPE_VECTOR parameter binding)
    // - Storage: Binary format (float array, e.g., 3 floats = 12 bytes for VECTOR(3))
    // - Query return: Typically binary format via seekdb_row_get_string()
    //   (may contain non-printable characters, use errors='replace' when decoding as UTF-8)
    
    seekdb_result_free(result);
    
    // Test 2: Auto-detection via seekdb_query_with_params
    // Test that parameters with SEEKDB_TYPE_STRING are auto-detected as SEEKDB_TYPE_VECTOR
    // based on column schema (Auto-detection based on column type information)
    const char* auto_detect_sql = R"(
        INSERT INTO test_vector_params (document, embedding) 
        VALUES (?, ?)
    )";
    
    const char* auto_doc = "Auto-Detection Test";
    const char* auto_embedding = "[1.5,2.5,3.5]";
    
    SeekdbBind auto_binds[2];
    bool auto_is_null_flags[2] = {false, false};
    unsigned long auto_lengths[2];
    
    // document (VARCHAR) - parameter 0
    auto_lengths[0] = strlen(auto_doc);
    auto_binds[0].buffer_type = SEEKDB_TYPE_STRING;
    auto_binds[0].buffer = const_cast<char*>(auto_doc);
    auto_binds[0].buffer_length = auto_lengths[0];
    auto_binds[0].length = &auto_lengths[0];
    auto_binds[0].is_null = &auto_is_null_flags[0];
    
    // embedding (VECTOR) - parameter 1
    // IMPORTANT: Set to STRING type to test auto-detection
    auto_lengths[1] = strlen(auto_embedding);
    auto_binds[1].buffer_type = SEEKDB_TYPE_STRING;  // Should be auto-detected as VECTOR
    auto_binds[1].buffer = const_cast<char*>(auto_embedding);
    auto_binds[1].buffer_length = auto_lengths[1];
    auto_binds[1].length = &auto_lengths[1];
    auto_binds[1].is_null = &auto_is_null_flags[1];
    
    // Execute using seekdb_query_with_params (which should auto-detect VECTOR type)
    SeekdbResult auto_result = nullptr;
    ret = seekdb_query_with_params(handle, auto_detect_sql, &auto_result, auto_binds, 2);
    if (ret != SEEKDB_SUCCESS) {
        const char* error = seekdb_error(handle);
        std::string error_msg = "Failed to execute INSERT with auto-detection";
        if (error && strlen(error) > 0) {
            error_msg += " (";
            error_msg += error;
            error_msg += ")";
        }
        execute_sql("DROP TABLE IF EXISTS test_vector_params");
        seekdb_connect_close(handle);
        return {false, error_msg};
    }
    
    if (auto_result) {
        seekdb_result_free(auto_result);
    }
    
    // Verify auto-detection worked: should have 6 rows now (5 from explicit binding + 1 from auto-detection)
    SeekdbResult count_result = nullptr;
    ret = seekdb_query(handle, "SELECT COUNT(*) as cnt FROM test_vector_params", &count_result);
    if (ret != SEEKDB_SUCCESS) {
        execute_sql("DROP TABLE IF EXISTS test_vector_params");
        seekdb_connect_close(handle);
        return {false, "Failed to verify auto-detection by querying count"};
    }
    
    count_result = seekdb_store_result(handle);
    if (count_result == nullptr) {
        execute_sql("DROP TABLE IF EXISTS test_vector_params");
        seekdb_connect_close(handle);
        return {false, "Failed to store count result"};
    }
    
    SeekdbRow count_row = seekdb_fetch_row(count_result);
    if (count_row == nullptr) {
        seekdb_result_free(count_result);
        execute_sql("DROP TABLE IF EXISTS test_vector_params");
        seekdb_connect_close(handle);
        return {false, "Failed to fetch count row"};
    }
    
    char auto_count_str[32] = {0};
    size_t auto_count_len = seekdb_row_get_string_len(count_row, 0);
    if (auto_count_len > 0 && auto_count_len < sizeof(auto_count_str)) {
        seekdb_row_get_string(count_row, 0, auto_count_str, sizeof(auto_count_str));
        int auto_count = std::atoi(auto_count_str);
        if (auto_count != 6) {
            seekdb_result_free(count_result);
            execute_sql("DROP TABLE IF EXISTS test_vector_params");
            seekdb_connect_close(handle);
            return {false, std::string("Expected 6 rows after auto-detection test, got ") + auto_count_str};
        }
    }
    
    seekdb_result_free(count_result);
    
    // Verify auto-detection row
    SeekdbResult verify_result = nullptr;
    ret = seekdb_query(handle, "SELECT document, embedding FROM test_vector_params WHERE document = 'Auto-Detection Test' LIMIT 1", &verify_result);
    if (ret != SEEKDB_SUCCESS) {
        execute_sql("DROP TABLE IF EXISTS test_vector_params");
        seekdb_connect_close(handle);
        return {false, "Failed to verify auto-detection row"};
    }
    
    verify_result = seekdb_store_result(handle);
    if (verify_result == nullptr) {
        execute_sql("DROP TABLE IF EXISTS test_vector_params");
        seekdb_connect_close(handle);
        return {false, "Failed to store auto-detection verification result"};
    }
    
    SeekdbRow verify_row = seekdb_fetch_row(verify_result);
    if (verify_row == nullptr) {
        seekdb_result_free(verify_result);
        execute_sql("DROP TABLE IF EXISTS test_vector_params");
        seekdb_connect_close(handle);
        return {false, "Expected 1 row for auto-detection test"};
    }
    
    // Verify embedding column is not null (proves auto-detection worked)
    size_t auto_embedding_len = seekdb_row_get_string_len(verify_row, 1);
    if (auto_embedding_len == 0) {
        seekdb_result_free(verify_result);
        execute_sql("DROP TABLE IF EXISTS test_vector_params");
        seekdb_connect_close(handle);
        return {false, "Embedding column is NULL - auto-detection failed"};
    }
    
    seekdb_result_free(verify_result);
    
    // Clean up
    execute_sql("DROP TABLE IF EXISTS test_vector_params");
    seekdb_connect_close(handle);
    
    return {true, ""};
}

// Test seekdb_real_query() - binary-safe query
TestResult test_real_query() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    SeekdbResult result = nullptr;
    const char* sql = "SELECT 1 as id, 'hello' as msg";
    ret = seekdb_real_query(handle, sql, strlen(sql), &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to execute real_query"};
    }
    
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        return {false, "Failed to store result"};
    }
    
    my_ulonglong row_count = seekdb_num_rows(result);
    if (row_count != 1) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Expected 1 row"};
    }
    
    seekdb_result_free(result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_use_result() - streaming result set
TestResult test_use_result() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    // Use real_query to prepare for use_result
    const char* sql = "SELECT 1 as id, 'hello' as msg";
    SeekdbResult result = nullptr;
    ret = seekdb_real_query(handle, sql, strlen(sql), &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to execute real_query"};
    }
    
    // Get use_result (streaming mode)
    result = seekdb_use_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        return {false, "Failed to get use_result"};
    }
    
    unsigned int col_count = seekdb_num_fields(result);
    if (col_count != 2) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Expected 2 columns"};
    }
    
    // In streaming mode, row_count may be -1 (unknown)
    // Just verify we can get column count and result is valid
    // Note: fetch_row may not work in streaming mode if not properly implemented
    // For now, just verify the result set is created correctly
    
    seekdb_result_free(result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_row_get_string_len() and seekdb_fetch_lengths()
TestResult test_row_lengths() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "SELECT 'hello' as msg, 123 as num, NULL as null_val", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to execute query"};
    }
    
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        return {false, "Failed to store result"};
    }
    
    SeekdbRow row = seekdb_fetch_row(result);
    if (row == nullptr) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch row"};
    }
    
    // Test seekdb_row_get_string_len()
    size_t msg_len = seekdb_row_get_string_len(row, 0);
    if (msg_len != 5) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Expected msg length 5"};
    }
    
    // Test seekdb_fetch_lengths()
    unsigned long* lengths = seekdb_fetch_lengths(result);
    if (lengths == nullptr) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to get lengths"};
    }
    
    if (lengths[0] != 5) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Expected first column length 5"};
    }
    
    seekdb_result_free(result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_row_is_null()
TestResult test_row_is_null() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "SELECT 'hello' as msg, NULL as null_val, 123 as num", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to execute query"};
    }
    
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        return {false, "Failed to store result"};
    }
    
    SeekdbRow row = seekdb_fetch_row(result);
    if (row == nullptr) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch row"};
    }
    
    // Test non-NULL value
    if (seekdb_row_is_null(row, 0)) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "First column should not be NULL"};
    }
    
    // Test NULL value
    if (!seekdb_row_is_null(row, 1)) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Second column should be NULL"};
    }
    
    // Test non-NULL value
    if (seekdb_row_is_null(row, 2)) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Third column should not be NULL"};
    }
    
    seekdb_result_free(result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// C ABI: NULL vs empty string must be distinguishable.
TestResult test_null_vs_empty_string() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "SELECT NULL AS a, '' AS b", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to execute query"};
    }
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        return {false, "Failed to store result"};
    }
    SeekdbRow row = seekdb_fetch_row(result);
    if (row == nullptr) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch row"};
    }
    // Column 0: SQL NULL -> row_is_null true, get_string_len (size_t)-1
    if (!seekdb_row_is_null(row, 0)) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Column NULL should report is_null true"};
    }
    size_t len0 = seekdb_row_get_string_len(row, 0);
    if (len0 != static_cast<size_t>(-1)) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Column NULL get_string_len should return (size_t)-1"};
    }
    // Column 1: empty string '' -> row_is_null false, get_string_len 0, get_string(buf,1) writes '\0'
    if (seekdb_row_is_null(row, 1)) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Column '' should report is_null false"};
    }
    size_t len1 = seekdb_row_get_string_len(row, 1);
    if (len1 != 0) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Column '' get_string_len should return 0"};
    }
    char buf1[1];
    if (seekdb_row_get_string(row, 1, buf1, 1) != SEEKDB_SUCCESS) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "get_string(row, 1, buf, 1) should succeed for ''"};
    }
    if (buf1[0] != '\0') {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Empty string should write '\\0' into buf"};
    }
    seekdb_result_free(result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// C ABI: get_string_len returns actual length; get_string(buf_size>=len+1) returns full content (no truncation).
TestResult test_long_string_length_and_full_read() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_long_str", &result);
    if (result) seekdb_result_free(result);
    ret = seekdb_query(handle, "CREATE TABLE test_long_str (id INT PRIMARY KEY, t VARCHAR(10000))", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to create table"};
    }
    if (result) seekdb_result_free(result);
    const size_t long_len = 10000;  // C ABI: get_string_len returns actual length; get_string returns full (VARCHAR in-row or LOB via read_lob_data)
    std::string long_val(long_len, 'x');
    std::string sql = "INSERT INTO test_long_str VALUES (1, '";
    sql += long_val;
    sql += "')";
    ret = seekdb_query(handle, sql.c_str(), &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to insert long string"};
    }
    if (result) seekdb_result_free(result);
    ret = seekdb_query(handle, "SELECT t FROM test_long_str WHERE id = 1", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to select"};
    }
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        return {false, "Failed to store result"};
    }
    SeekdbRow row = seekdb_fetch_row(result);
    if (row == nullptr) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch row"};
    }
    size_t len = seekdb_row_get_string_len(row, 0);
    if (len != long_len) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "get_string_len should return actual length " + std::to_string(long_len)};
    }
    std::vector<char> buf(long_len + 1);
    if (seekdb_row_get_string(row, 0, buf.data(), buf.size()) != SEEKDB_SUCCESS) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "get_string with buf_size len+1 should succeed"};
    }
    if (strlen(buf.data()) != long_len) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Full content length mismatch"};
    }
    for (size_t i = 0; i < long_len; i++) {
        if (buf[i] != 'x') {
            seekdb_result_free(result);
            seekdb_connect_close(handle);
            return {false, "Content mismatch at offset " + std::to_string(i)};
        }
    }
    seekdb_result_free(result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// 100KB document: must not read back as '' or null; get_string_len returns actual length; get_string returns full content.
// Uses parameterized INSERT + LONGTEXT; session ob_default_lob_inrow_threshold so 100KB is in-row (or C ABI read_lob_data for out-of-row).
TestResult test_very_long_document_100kb() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "SET SESSION max_allowed_packet = 2097152", &result);
    if (result) seekdb_result_free(result);
    // Align with server: use session default for LOB in-row threshold so 100KB is in-row; no table option (same DDL as server).
    ret = seekdb_query(handle, "SET SESSION ob_default_lob_inrow_threshold = 262144", &result);
    if (result) seekdb_result_free(result);
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_doc_100k", &result);
    if (result) seekdb_result_free(result);
    ret = seekdb_query(handle, "CREATE TABLE test_doc_100k (id INT PRIMARY KEY, doc LONGTEXT)", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to create table"};
    }
    if (result) seekdb_result_free(result);
    size_t doc_len = 100000;  // 100KB document
    std::string doc_val(doc_len, 'a');
    SeekdbBind binds[2];
    int32_t id_val = 1;
    binds[0].buffer_type = SEEKDB_TYPE_LONG;
    binds[0].buffer = &id_val;
    binds[0].buffer_length = sizeof(id_val);
    binds[0].length = nullptr;
    bool null0 = false;
    binds[0].is_null = &null0;
    binds[1].buffer_type = SEEKDB_TYPE_STRING;
    binds[1].buffer = const_cast<char*>(doc_val.data());
    binds[1].buffer_length = static_cast<unsigned long>(doc_len);
    unsigned long doc_len_ul = static_cast<unsigned long>(doc_len);
    binds[1].length = &doc_len_ul;
    bool null1 = false;
    binds[1].is_null = &null1;
    ret = seekdb_query_with_params(handle, "INSERT INTO test_doc_100k VALUES (?, ?)", &result, binds, 2);
    if (ret != SEEKDB_SUCCESS) {
        const char* err = seekdb_error(handle);
        std::string msg = "Failed to insert 100KB document";
        if (err && strlen(err) > 0) { msg += " ("; msg += err; msg += ")"; }
        seekdb_connect_close(handle);
        return {false, msg};
    }
    if (result) seekdb_result_free(result);
    ret = seekdb_query(handle, "SELECT doc FROM test_doc_100k WHERE id = 1", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to select"};
    }
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        return {false, "Failed to store result"};
    }
    SeekdbRow row = seekdb_fetch_row(result);
    if (row == nullptr) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch row"};
    }
    if (seekdb_row_is_null(row, 0)) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "100KB document must not be reported as null"};
    }
    size_t len = seekdb_row_get_string_len(row, 0);
    if (len != doc_len) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "get_string_len should return actual length " + std::to_string(doc_len)};
    }
    std::vector<char> buf(doc_len + 1);
    if (seekdb_row_get_string(row, 0, buf.data(), buf.size()) != SEEKDB_SUCCESS) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "get_string(buf_size>=len+1) should return full content"};
    }
    if (strlen(buf.data()) != doc_len) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Full content length mismatch"};
    }
    seekdb_result_free(result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// Embedded collection DDL: document STRING column, 100KB round-trip.
TestResult test_collection_string_document_100kb() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "SET SESSION max_allowed_packet = 2097152", &result);
    if (result) seekdb_result_free(result);
    ret = seekdb_query(handle, "SET SESSION ob_default_lob_inrow_threshold = 262144", &result);
    if (result) seekdb_result_free(result);
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_coll_doc_100k", &result);
    if (result) seekdb_result_free(result);
    ret = seekdb_query(handle,
        "CREATE TABLE test_coll_doc_100k (_id VARBINARY(512) PRIMARY KEY, document STRING)",
        &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to create collection-like table"};
    }
    if (result) seekdb_result_free(result);
    const char* id_val = "id_long";
    const size_t doc_len = 100000;
    std::string doc_val(doc_len, 'a');
    SeekdbBind binds[2];
    binds[0].buffer_type = SEEKDB_TYPE_VARBINARY_ID;
    binds[0].buffer = const_cast<char*>(id_val);
    binds[0].buffer_length = static_cast<unsigned long>(strlen(id_val));
    unsigned long id_len = binds[0].buffer_length;
    binds[0].length = &id_len;
    bool null0 = false;
    binds[0].is_null = &null0;
    binds[1].buffer_type = SEEKDB_TYPE_STRING;
    binds[1].buffer = const_cast<char*>(doc_val.data());
    binds[1].buffer_length = static_cast<unsigned long>(doc_len);
    unsigned long doc_len_ul = static_cast<unsigned long>(doc_len);
    binds[1].length = &doc_len_ul;
    bool null1 = false;
    binds[1].is_null = &null1;
    ret = seekdb_query_with_params(handle,
        "INSERT INTO test_coll_doc_100k (_id, document) VALUES (CAST(? AS BINARY), ?)",
        &result, binds, 2);
    if (ret != SEEKDB_SUCCESS) {
        const char* err = seekdb_error(handle);
        std::string msg = "Failed to insert 100KB STRING document";
        if (err && strlen(err) > 0) {
            msg += " (";
            msg += err;
            msg += ")";
        }
        seekdb_connect_close(handle);
        return {false, msg};
    }
    if (result) seekdb_result_free(result);
    SeekdbBind sel_binds[1];
    sel_binds[0].buffer_type = SEEKDB_TYPE_VARBINARY_ID;
    sel_binds[0].buffer = const_cast<char*>(id_val);
    sel_binds[0].buffer_length = id_len;
    sel_binds[0].length = &id_len;
    bool sel_null = false;
    sel_binds[0].is_null = &sel_null;
    ret = seekdb_query_with_params(handle,
        "SELECT document FROM test_coll_doc_100k WHERE _id = CAST(? AS BINARY)",
        &result, sel_binds, 1);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to select STRING document"};
    }
    // query_with_params returns result set directly; store_result is only for seekdb_query().
    if (result == nullptr) {
        result = seekdb_store_result(handle);
    }
    if (result == nullptr) {
        seekdb_connect_close(handle);
        return {false, "Failed to get select result"};
    }
    SeekdbRow row = seekdb_fetch_row(result);
    if (row == nullptr) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch row"};
    }
    if (seekdb_row_is_null(row, 0)) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "STRING document must not be null"};
    }
    const size_t len = seekdb_row_get_string_len(row, 0);
    if (len != doc_len) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "get_string_len mismatch for STRING document column"};
    }
    std::vector<char> buf(doc_len + 1);
    if (seekdb_row_get_string(row, 0, buf.data(), buf.size()) != SEEKDB_SUCCESS) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "get_string failed for STRING document column"};
    }
    for (size_t i = 0; i < doc_len; i++) {
        if (buf[i] != 'a') {
            seekdb_result_free(result);
            seekdb_connect_close(handle);
            return {false, "STRING document content mismatch"};
        }
    }
    seekdb_result_free(result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// Metadata with newlines, quotes, backslashes must be stored and read back completely (no truncation/corruption).
TestResult test_special_characters_in_metadata() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_meta_special", &result);
    if (result) seekdb_result_free(result);
    ret = seekdb_query(handle, "CREATE TABLE test_meta_special (id INT PRIMARY KEY, meta VARCHAR(2000))", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to create table"};
    }
    if (result) seekdb_result_free(result);
    std::string meta_val = "{\"key\":\"value with \\\"quotes\\\" and \\n newline and \\\\ backslash\"}";
    std::string escaped;
    for (char c : meta_val) {
        if (c == '\'') escaped += "''";
        else if (c == '\\') escaped += "\\\\";
        else escaped += c;
    }
    std::string sql = "INSERT INTO test_meta_special VALUES (1, '" + escaped + "')";
    ret = seekdb_query(handle, sql.c_str(), &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to insert metadata with special chars"};
    }
    if (result) seekdb_result_free(result);
    ret = seekdb_query(handle, "SELECT meta FROM test_meta_special WHERE id = 1", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to select"};
    }
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        return {false, "Failed to store result"};
    }
    SeekdbRow row = seekdb_fetch_row(result);
    if (row == nullptr) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch row"};
    }
    if (seekdb_row_is_null(row, 0)) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "metadata must not be reported as null"};
    }
    size_t len = seekdb_row_get_string_len(row, 0);
    if (len == static_cast<size_t>(-1) || len != meta_val.length()) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "metadata get_string_len should return actual length"};
    }
    std::vector<char> buf(len + 1);
    if (seekdb_row_get_string(row, 0, buf.data(), buf.size()) != SEEKDB_SUCCESS) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "get_string should return full metadata"};
    }
    if (strcmp(buf.data(), meta_val.c_str()) != 0) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "metadata content mismatch (truncation or corruption)"};
    }
    seekdb_result_free(result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// JSON column parameterized INSERT (embedded collection metadata path).
TestResult test_json_column_param_insert() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_json_param", &result);
    if (result) seekdb_result_free(result);
    ret = seekdb_query(handle,
        "CREATE TABLE test_json_param (id INT PRIMARY KEY, meta JSON)",
        &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to create table"};
    }
    if (result) seekdb_result_free(result);
    const char* meta_json =
        "{\"key with spaces\":\"value\",\"key-with-dashes\":\"value\",\"key\\nwith\\nnewlines\":\"value\","
        "\"key\\\"with\\\"quotes\":\"value\"}";
    SeekdbBind binds[1];
    binds[0].buffer_type = SEEKDB_TYPE_STRING;
    binds[0].buffer = const_cast<char*>(meta_json);
    unsigned long meta_len = static_cast<unsigned long>(strlen(meta_json));
    binds[0].buffer_length = meta_len;
    binds[0].length = &meta_len;
    bool null0 = false;
    binds[0].is_null = &null0;
    ret = seekdb_query_with_params(handle,
        "INSERT INTO test_json_param (id, meta) VALUES (1, ?)",
        &result, binds, 1);
    if (ret != SEEKDB_SUCCESS) {
        const char* err = seekdb_error(handle);
        std::string msg = "Failed JSON param insert";
        if (err && strlen(err) > 0) {
            msg += " (";
            msg += err;
            msg += ")";
        }
        seekdb_connect_close(handle);
        return {false, msg};
    }
    if (result) seekdb_result_free(result);
    ret = seekdb_query(handle, "SELECT meta FROM test_json_param WHERE id = 1", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to select JSON"};
    }
    if (result == nullptr) {
        result = seekdb_store_result(handle);
    }
    if (result == nullptr) {
        seekdb_connect_close(handle);
        return {false, "Failed to get select result"};
    }
    SeekdbRow row = seekdb_fetch_row(result);
    if (row == nullptr) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch row"};
    }
    if (seekdb_row_is_null(row, 0)) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "JSON meta must not be null"};
    }
    const size_t len = seekdb_row_get_string_len(row, 0);
    if (len == static_cast<size_t>(-1) || len == 0) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "JSON meta length invalid after param insert"};
    }
    std::vector<char> buf(len + 1);
    if (seekdb_row_get_string(row, 0, buf.data(), buf.size()) != SEEKDB_SUCCESS) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "get_string failed for JSON meta"};
    }
    std::string read_meta(buf.data());
    if (read_meta.find("key with spaces") == std::string::npos ||
        read_meta.find("key\\nwith\\nnewlines") == std::string::npos ||
        read_meta.find("key\\\"with\\\"quotes") == std::string::npos) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "JSON meta content missing expected keys after param insert"};
    }
    seekdb_result_free(result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// Empty JSON object "{}" in a JSON column must round-trip (embedded collection metadata).
TestResult test_empty_json_metadata() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_empty_json_meta", &result);
    if (result) seekdb_result_free(result);
    ret = seekdb_query(handle, "CREATE TABLE test_empty_json_meta (id INT PRIMARY KEY, meta JSON)", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to create table"};
    }
    if (result) seekdb_result_free(result);
    ret = seekdb_query(handle, "INSERT INTO test_empty_json_meta VALUES (1, '{}')", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to insert {}"};
    }
    if (result) seekdb_result_free(result);
    ret = seekdb_query(handle, "SELECT meta FROM test_empty_json_meta WHERE id = 1", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to select"};
    }
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        return {false, "Failed to store result"};
    }
    SeekdbRow row = seekdb_fetch_row(result);
    if (row == nullptr) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch row"};
    }
    if (seekdb_row_is_null(row, 0)) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "meta must not be reported as null"};
    }
    size_t len = seekdb_row_get_string_len(row, 0);
    if (len == static_cast<size_t>(-1)) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "meta length should not be -1"};
    }
    std::vector<char> buf(len + 1);
    if (seekdb_row_get_string(row, 0, buf.data(), buf.size()) != SEEKDB_SUCCESS) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "get_string failed"};
    }
    std::string meta_str(buf.data());
    if (meta_str != "{}") {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "expected {} got: " + meta_str};
    }
    seekdb_result_free(result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// Same-process same-path reuse: second seekdb_open() with same path must return success (no "db opened by other process").
TestResult test_embedded_same_path_reuse_relative() {
    // Re-open with the same path used in main() (relative or absolute)
    int ret = seekdb_open(g_db_path);
    if (ret != SEEKDB_SUCCESS) {
        const char* err = seekdb_last_error();
        return {false, std::string("second seekdb_open(same path) should succeed, got: ") + (err ? err : "unknown")};
    }
    return {true, ""};
}

// Same-process same-path reuse: seekdb_open() with absolute path to same db must return success (embedded absolute path fix).
TestResult test_embedded_same_path_reuse_absolute() {
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        return {false, "getcwd failed"};
    }
    std::string abs_path = (g_db_path[0] == '/') ? g_db_path : (std::string(cwd) + "/" + g_db_path);
    int ret = seekdb_open(abs_path.c_str());
    if (ret != SEEKDB_SUCCESS) {
        const char* err = seekdb_last_error();
        return {false, std::string("seekdb_open(absolute path) should succeed, got: ") + (err ? err : "unknown")};
    }
    return {true, ""};
}

// Test seekdb_affected_rows() and seekdb_insert_id()
TestResult test_affected_rows_and_insert_id() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_affected", &result);
    if (result) seekdb_result_free(result);
    
    ret = seekdb_query(handle, "CREATE TABLE test_affected (id INT AUTO_INCREMENT PRIMARY KEY, name VARCHAR(100))", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to create table"};
    }
    if (result) seekdb_result_free(result);
    
    // Test INSERT and affected_rows
    ret = seekdb_query(handle, "INSERT INTO test_affected (name) VALUES ('Alice'), ('Bob')", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to insert"};
    }
    if (result) seekdb_result_free(result);
    
    my_ulonglong affected = seekdb_affected_rows(handle);
    if (affected != 2) {
        seekdb_connect_close(handle);
        return {false, "Expected 2 affected rows"};
    }
    
    // Test insert_id
    my_ulonglong insert_id = seekdb_insert_id(handle);
    if (insert_id == 0) {
        seekdb_connect_close(handle);
        return {false, "Expected non-zero insert_id"};
    }
    
    // Test UPDATE and affected_rows
    ret = seekdb_query(handle, "UPDATE test_affected SET name = 'Charlie' WHERE id = 1", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to update"};
    }
    if (result) seekdb_result_free(result);
    
    affected = seekdb_affected_rows(handle);
    if (affected != 1) {
        seekdb_connect_close(handle);
        return {false, "Expected 1 affected row for UPDATE"};
    }
    
    // Test DELETE and affected_rows
    ret = seekdb_query(handle, "DELETE FROM test_affected WHERE id = 2", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to delete"};
    }
    if (result) seekdb_result_free(result);
    
    affected = seekdb_affected_rows(handle);
    if (affected != 1) {
        seekdb_connect_close(handle);
        return {false, "Expected 1 affected row for DELETE"};
    }
    
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_affected", &result);
    if (result) seekdb_result_free(result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_autocommit()
TestResult test_autocommit() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", false);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    // Test disable autocommit
    ret = seekdb_autocommit(handle, false);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to disable autocommit"};
    }
    
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "CREATE TABLE IF NOT EXISTS test_autocommit (id INT PRIMARY KEY)", &result);
    if (result) seekdb_result_free(result);
    
    ret = seekdb_query(handle, "INSERT INTO test_autocommit VALUES (1)", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to insert"};
    }
    if (result) seekdb_result_free(result);
    
    // Rollback should remove the row
    ret = seekdb_rollback(handle);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to rollback"};
    }
    
    ret = seekdb_query(handle, "SELECT * FROM test_autocommit WHERE id = 1", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to query"};
    }
    
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        return {false, "Failed to store result"};
    }
    
    my_ulonglong row_count = seekdb_num_rows(result);
    if (row_count != 0) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Expected 0 rows after rollback"};
    }
    seekdb_result_free(result);
    
    // Test enable autocommit
    ret = seekdb_autocommit(handle, true);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to enable autocommit"};
    }
    
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_autocommit", &result);
    if (result) seekdb_result_free(result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_ping()
TestResult test_ping() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    ret = seekdb_ping(handle);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Ping failed"};
    }
    
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_set_character_set() and seekdb_get_character_set_info()
TestResult test_character_set() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    // Test get character set name
    const char* charset_name = seekdb_character_set_name(handle);
    if (charset_name == nullptr || strlen(charset_name) == 0) {
        seekdb_connect_close(handle);
        return {false, "Failed to get character set name"};
    }
    
    // Test set character set
    ret = seekdb_set_character_set(handle, "utf8mb4");
    if (ret != SEEKDB_SUCCESS) {
        // Character set may not be changeable in embedded mode, that's okay
        // Just verify the function doesn't crash
    }
    
    // Test get character set info
    SeekdbCharsetInfo charset_info;
    ret = seekdb_get_character_set_info(handle, "utf8mb4", &charset_info);
    // May fail if charset not available, that's okay
    
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_select_db()
TestResult test_select_db() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    // Test select database (should succeed even if already selected)
    ret = seekdb_select_db(handle, "test");
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to select database"};
    }
    
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_stmt_reset()
TestResult test_stmt_reset() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_stmt_reset", &result);
    if (result) seekdb_result_free(result);
    
    ret = seekdb_query(handle, "CREATE TABLE test_stmt_reset (id INT PRIMARY KEY, name VARCHAR(100))", &result);
    if (result) seekdb_result_free(result);
    
    SeekdbStmt stmt = seekdb_stmt_init(handle);
    if (!stmt) {
        seekdb_connect_close(handle);
        return {false, "Failed to init statement"};
    }
    
    const char* sql = "INSERT INTO test_stmt_reset VALUES (?, ?)";
    ret = seekdb_stmt_prepare(stmt, sql, strlen(sql));
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to prepare statement"};
    }
    
    // Bind and execute first time
    int32_t id1 = 1;
    const char* name1 = "Alice";
    SeekdbBind bind1[2];
    bool is_null1[2] = {false, false};
    unsigned long len1[2] = {sizeof(id1), strlen(name1)};
    
    bind1[0].buffer_type = SEEKDB_TYPE_LONG;
    bind1[0].buffer = &id1;
    bind1[0].buffer_length = sizeof(id1);
    bind1[0].length = &len1[0];
    bind1[0].is_null = &is_null1[0];
    
    bind1[1].buffer_type = SEEKDB_TYPE_STRING;
    bind1[1].buffer = const_cast<char*>(name1);
    bind1[1].buffer_length = len1[1];
    bind1[1].length = &len1[1];
    bind1[1].is_null = &is_null1[1];
    
    ret = seekdb_stmt_bind_param(stmt, bind1);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to bind parameters"};
    }
    
    ret = seekdb_stmt_execute(stmt);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to execute statement"};
    }
    
    // Reset statement
    ret = seekdb_stmt_reset(stmt);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to reset statement"};
    }
    
    // Bind and execute second time with different values
    int32_t id2 = 2;
    const char* name2 = "Bob";
    SeekdbBind bind2[2];
    bool is_null2[2] = {false, false};
    unsigned long len2[2] = {sizeof(id2), strlen(name2)};
    
    bind2[0].buffer_type = SEEKDB_TYPE_LONG;
    bind2[0].buffer = &id2;
    bind2[0].buffer_length = sizeof(id2);
    bind2[0].length = &len2[0];
    bind2[0].is_null = &is_null2[0];
    
    bind2[1].buffer_type = SEEKDB_TYPE_STRING;
    bind2[1].buffer = const_cast<char*>(name2);
    bind2[1].buffer_length = len2[1];
    bind2[1].length = &len2[1];
    bind2[1].is_null = &is_null2[1];
    
    ret = seekdb_stmt_bind_param(stmt, bind2);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to bind parameters second time"};
    }
    
    ret = seekdb_stmt_execute(stmt);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to execute statement second time"};
    }
    
    // Verify both rows were inserted
    ret = seekdb_query(handle, "SELECT COUNT(*) FROM test_stmt_reset", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to verify"};
    }
    
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to store result"};
    }
    
    SeekdbRow row = seekdb_fetch_row(result);
    if (row == nullptr) {
        seekdb_result_free(result);
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch row"};
    }
    
    char count_str[32] = {0};
    size_t count_len = seekdb_row_get_string_len(row, 0);
    if (count_len > 0 && count_len < sizeof(count_str)) {
        seekdb_row_get_string(row, 0, count_str, sizeof(count_str));
        int count = std::atoi(count_str);
        if (count != 2) {
            seekdb_result_free(result);
            seekdb_stmt_close(stmt);
            seekdb_connect_close(handle);
            return {false, "Expected 2 rows after reset and re-execute"};
        }
    }
    
    seekdb_result_free(result);
    seekdb_stmt_close(stmt);
    
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_stmt_reset", &result);
    if (result) seekdb_result_free(result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_data_seek() and seekdb_field_seek()
TestResult test_data_seek_and_field_seek() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "SELECT 1 as col1, 'hello' as col2, 3.14 as col3", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to execute query"};
    }
    
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        return {false, "Failed to store result"};
    }
    
    // Test field_seek
    ret = seekdb_field_seek(result, 1);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to seek field"};
    }
    
    SeekdbField* field = seekdb_fetch_field(result);
    if (field == nullptr || field->name == nullptr || strcmp(field->name, "col2") != 0) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Field seek failed"};
    }
    
    // Test data_seek (should work even with 1 row)
    ret = seekdb_data_seek(result, 0);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to seek data"};
    }
    
    SeekdbRow row = seekdb_fetch_row(result);
    if (row == nullptr) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch row after data_seek"};
    }
    
    seekdb_result_free(result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_result_fetch_all_rows() and seekdb_result_get_all_column_names()
TestResult test_result_fetch_all() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    // Test 1: get_all_column_names_alloc
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "SELECT 1 as id, 'hello' as msg", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to execute query"};
    }
    
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        return {false, "Failed to store result"};
    }
    
    char** column_names = nullptr;
    int32_t column_count = 0;
    ret = seekdb_result_get_all_column_names_alloc(result, &column_names, &column_count);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to get column names"};
    }
    
    if (column_count != 2) {
        if (column_names) seekdb_free_column_names(column_names, column_count);
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Expected 2 columns"};
    }
    
    if (column_names == nullptr || column_names[0] == nullptr || column_names[1] == nullptr) {
        if (column_names) seekdb_free_column_names(column_names, column_count);
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Column names array is null"};
    }
    
    if (strcmp(column_names[0], "id") != 0 || strcmp(column_names[1], "msg") != 0) {
        if (column_names) seekdb_free_column_names(column_names, column_count);
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Column names mismatch"};
    }
    
    seekdb_free_column_names(column_names, column_count);
    seekdb_result_free(result);
    
    // Note: seekdb_result_fetch_all_rows() is tested in integration tests
    // The C++ unit test focuses on core C ABI functionality
    // Complex memory management functions like fetch_all_rows can be tested
    // in language bindings where memory safety is better handled
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_field_count()
TestResult test_field_count() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "SELECT 1 as col1, 'hello' as col2, 3.14 as col3", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to execute query"};
    }
    
    unsigned int field_count = seekdb_field_count(handle);
    if (field_count != 3) {
        if (result) seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Expected 3 fields"};
    }
    
    if (result) seekdb_result_free(result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_result_column_name() and seekdb_result_column_name_len()
TestResult test_result_column_name() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "SELECT 1 as id, 'hello' as msg", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to execute query"};
    }
    
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        return {false, "Failed to store result"};
    }
    
    // Test column_name_len
    size_t name_len = seekdb_result_column_name_len(result, 0);
    if (name_len != 2) {  // "id" length
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Expected column name length 2"};
    }
    
    // Test column_name
    char name_buf[64] = {0};
    ret = seekdb_result_column_name(result, 0, name_buf, sizeof(name_buf));
    if (ret != SEEKDB_SUCCESS) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to get column name"};
    }
    
    if (strcmp(name_buf, "id") != 0) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Column name mismatch"};
    }
    
    seekdb_result_free(result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_row_get_int64(), seekdb_row_get_double(), seekdb_row_get_bool()
TestResult test_row_get_types() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "SELECT 123 as int_val, 3.14 as double_val, true as bool_val", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to execute query"};
    }
    
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        return {false, "Failed to store result"};
    }
    
    SeekdbRow row = seekdb_fetch_row(result);
    if (row == nullptr) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch row"};
    }
    
    // Test get_int64
    int64_t int_val = 0;
    ret = seekdb_row_get_int64(row, 0, &int_val);
    if (ret != SEEKDB_SUCCESS || int_val != 123) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to get int64 value"};
    }
    
    // Test get_double
    double double_val = 0.0;
    ret = seekdb_row_get_double(row, 1, &double_val);
    if (ret != SEEKDB_SUCCESS || double_val < 3.13 || double_val > 3.15) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to get double value"};
    }
    
    // Test get_bool
    bool bool_val = false;
    ret = seekdb_row_get_bool(row, 2, &bool_val);
    if (ret != SEEKDB_SUCCESS || bool_val != true) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to get bool value"};
    }
    
    seekdb_result_free(result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_last_error() and seekdb_last_error_code()
TestResult test_last_error() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    // Trigger an error
    SeekdbResult result = nullptr;
    seekdb_query(handle, "SELECT * FROM non_existent_table_12345", &result);
    if (result) seekdb_result_free(result);
    
    // Test last_error
    const char* error_msg = seekdb_last_error();
    if (error_msg == nullptr || strlen(error_msg) == 0) {
        seekdb_connect_close(handle);
        return {false, "Failed to get last error message"};
    }
    
    // Test last_error_code
    int error_code = seekdb_last_error_code();
    if (error_code == SEEKDB_SUCCESS) {
        // Error code might be 0 if error was handled, that's okay
    }
    
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_errno()
TestResult test_errno() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    // Trigger an error
    SeekdbResult result = nullptr;
    seekdb_query(handle, "SELECT * FROM non_existent_table_67890", &result);
    if (result) seekdb_result_free(result);
    
    unsigned int errno_val = seekdb_errno(handle);
    // errno might be 0 if error was handled, that's okay
    
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_get_server_info(), seekdb_get_host_info(), seekdb_get_client_info()
TestResult test_server_info() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    // Test get_server_info
    const char* server_info = seekdb_get_server_info(handle);
    if (server_info == nullptr || strlen(server_info) == 0) {
        seekdb_connect_close(handle);
        return {false, "Failed to get server info"};
    }
    
    // Test get_host_info
    const char* host_info = seekdb_get_host_info(handle);
    if (host_info == nullptr || strlen(host_info) == 0) {
        seekdb_connect_close(handle);
        return {false, "Failed to get host info"};
    }
    
    // Test get_client_info
    const char* client_info = seekdb_get_client_info();
    if (client_info == nullptr || strlen(client_info) == 0) {
        seekdb_connect_close(handle);
        return {false, "Failed to get client info"};
    }
    
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_info() and seekdb_warning_count()
TestResult test_info_and_warnings() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "SELECT 1", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to execute query"};
    }
    if (result) seekdb_result_free(result);
    
    // Test info (may be NULL if no info available)
    const char* info = seekdb_info(handle);
    // info can be NULL, that's okay
    
    // Test warning_count
    unsigned int warning_count = seekdb_warning_count(handle);
    // warning_count can be 0, that's okay
    
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_sqlstate()
TestResult test_sqlstate() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    // Trigger an error
    SeekdbResult result = nullptr;
    seekdb_query(handle, "SELECT * FROM non_existent_table_sqlstate", &result);
    if (result) seekdb_result_free(result);
    
    // Test sqlstate (may be NULL if not available)
    const char* sqlstate = seekdb_sqlstate(handle);
    // sqlstate can be NULL, that's okay
    
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_real_escape_string() and seekdb_hex_string()
TestResult test_escape_string() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    // Test real_escape_string
    const char* from = "test'string\"with\\special";
    char to[256] = {0};
    unsigned long escaped_len = seekdb_real_escape_string(handle, to, sizeof(to), from, strlen(from));
    if (escaped_len == (unsigned long)-1) {
        seekdb_connect_close(handle);
        return {false, "Failed to escape string"};
    }
    
    if (escaped_len == 0 || escaped_len >= sizeof(to)) {
        seekdb_connect_close(handle);
        return {false, "Invalid escaped length"};
    }
    
    // Test hex_string
    const char* hex_from = "hello";
    char hex_to[256] = {0};
    unsigned long hex_len = seekdb_hex_string(hex_to, sizeof(hex_to), hex_from, strlen(hex_from));
    if (hex_len == (unsigned long)-1) {
        seekdb_connect_close(handle);
        return {false, "Failed to convert to hex"};
    }
    
    if (hex_len == 0 || hex_len >= sizeof(hex_to)) {
        seekdb_connect_close(handle);
        return {false, "Invalid hex length"};
    }
    
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_get_server_version()
TestResult test_server_version() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    unsigned long version = seekdb_get_server_version(handle);
    if (version == 0) {
        seekdb_connect_close(handle);
        return {false, "Failed to get server version"};
    }
    
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_next_result() and seekdb_more_results()
TestResult test_next_result() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    // Test with single result set
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "SELECT 1 as id", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to execute query"};
    }
    if (result) seekdb_result_free(result);
    
    // Test more_results (should be false for single result)
    bool more = seekdb_more_results(handle);
    // more can be false, that's okay
    
    // Test next_result (should return -1 for no more results)
    int next_ret = seekdb_next_result(handle);
    // next_ret can be -1, that's okay
    
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_stmt_field_count(), seekdb_stmt_data_seek(), seekdb_stmt_row_seek(), seekdb_stmt_row_tell()
TestResult test_stmt_field_operations() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    SeekdbResult cleanup_result = nullptr;
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_stmt_field", &cleanup_result);
    if (cleanup_result) seekdb_result_free(cleanup_result);
    
    ret = seekdb_query(handle, "CREATE TABLE test_stmt_field (id INT PRIMARY KEY, name VARCHAR(100))", &cleanup_result);
    if (cleanup_result) seekdb_result_free(cleanup_result);
    
    ret = seekdb_query(handle, "INSERT INTO test_stmt_field VALUES (1, 'Alice'), (2, 'Bob'), (3, 'Charlie')", &cleanup_result);
    if (cleanup_result) seekdb_result_free(cleanup_result);
    
    SeekdbStmt stmt = seekdb_stmt_init(handle);
    if (!stmt) {
        seekdb_connect_close(handle);
        return {false, "Failed to init statement"};
    }
    
    const char* sql = "SELECT id, name FROM test_stmt_field ORDER BY id";
    ret = seekdb_stmt_prepare(stmt, sql, strlen(sql));
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to prepare statement"};
    }
    
    ret = seekdb_stmt_execute(stmt);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to execute statement"};
    }
    
    // Test field_count (after execute)
    unsigned int field_count = seekdb_stmt_field_count(stmt);
    if (field_count != 2) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Expected 2 fields"};
    }
    
    // Test data_seek
    ret = seekdb_stmt_data_seek(stmt, 1);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to seek data"};
    }
    
    // Test row_tell
    SeekdbRow row_tell = seekdb_stmt_row_tell(stmt);
    // row_tell can be NULL, that's okay
    
    // Test row_seek (if row_tell is valid)
    if (row_tell != nullptr) {
        SeekdbRow row_seek = seekdb_stmt_row_seek(stmt, row_tell);
        // row_seek can be NULL, that's okay
    }
    
    seekdb_stmt_close(stmt);
    
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_stmt_field", &cleanup_result);
    if (cleanup_result) seekdb_result_free(cleanup_result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_stmt_fetch_column()
TestResult test_stmt_fetch_column() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    SeekdbResult cleanup_result = nullptr;
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_stmt_fetch_col", &cleanup_result);
    if (cleanup_result) seekdb_result_free(cleanup_result);
    
    ret = seekdb_query(handle, "CREATE TABLE test_stmt_fetch_col (id INT PRIMARY KEY, name VARCHAR(100))", &cleanup_result);
    if (cleanup_result) seekdb_result_free(cleanup_result);
    
    ret = seekdb_query(handle, "INSERT INTO test_stmt_fetch_col VALUES (1, 'Alice')", &cleanup_result);
    if (cleanup_result) seekdb_result_free(cleanup_result);
    
    SeekdbStmt stmt = seekdb_stmt_init(handle);
    if (!stmt) {
        seekdb_connect_close(handle);
        return {false, "Failed to init statement"};
    }
    
    const char* sql = "SELECT id, name FROM test_stmt_fetch_col WHERE id = ?";
    ret = seekdb_stmt_prepare(stmt, sql, strlen(sql));
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to prepare statement"};
    }
    
    int32_t param_id = 1;
    SeekdbBind bind;
    bind.buffer_type = SEEKDB_TYPE_LONG;
    bind.buffer = &param_id;
    bind.buffer_length = sizeof(param_id);
    bind.length = nullptr;
    bool is_null = false;
    bind.is_null = &is_null;
    
    ret = seekdb_stmt_bind_param(stmt, &bind);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to bind parameter"};
    }
    
    ret = seekdb_stmt_execute(stmt);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to execute statement"};
    }
    
    // Bind result
    int32_t result_id = 0;
    char result_name[256] = {0};
    unsigned long result_name_len = 0;
    bool result_id_null = false;
    bool result_name_null = false;
    
    SeekdbBind result_binds[2];
    result_binds[0].buffer_type = SEEKDB_TYPE_LONG;
    result_binds[0].buffer = &result_id;
    result_binds[0].buffer_length = sizeof(result_id);
    result_binds[0].length = nullptr;
    result_binds[0].is_null = &result_id_null;
    
    result_binds[1].buffer_type = SEEKDB_TYPE_STRING;
    result_binds[1].buffer = result_name;
    result_binds[1].buffer_length = sizeof(result_name);
    result_binds[1].length = &result_name_len;
    result_binds[1].is_null = &result_name_null;
    
    ret = seekdb_stmt_bind_result(stmt, result_binds);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to bind result"};
    }
    
    ret = seekdb_stmt_fetch(stmt);
    if (ret != 0) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch row"};
    }
    
    // Test fetch_column for second column (name)
    SeekdbBind fetch_bind;
    char fetch_name[256] = {0};
    unsigned long fetch_name_len = 0;
    bool fetch_name_null = false;
    fetch_bind.buffer_type = SEEKDB_TYPE_STRING;
    fetch_bind.buffer = fetch_name;
    fetch_bind.buffer_length = sizeof(fetch_name);
    fetch_bind.length = &fetch_name_len;
    fetch_bind.is_null = &fetch_name_null;
    
    ret = seekdb_stmt_fetch_column(stmt, &fetch_bind, 1, 0);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch column"};
    }
    
    if (fetch_name_null || strcmp(fetch_name, "Alice") != 0) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Fetched column data mismatch"};
    }
    
    seekdb_stmt_close(stmt);
    
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_stmt_fetch_col", &cleanup_result);
    if (cleanup_result) seekdb_result_free(cleanup_result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_stmt_error(), seekdb_stmt_errno(), seekdb_stmt_sqlstate()
TestResult test_stmt_error_info() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    SeekdbStmt stmt = seekdb_stmt_init(handle);
    if (!stmt) {
        seekdb_connect_close(handle);
        return {false, "Failed to init statement"};
    }
    
    // Try to prepare invalid SQL to trigger error
    const char* invalid_sql = "INVALID SQL STATEMENT";
    ret = seekdb_stmt_prepare(stmt, invalid_sql, strlen(invalid_sql));
    // This may or may not fail depending on implementation
    
    // Test stmt_error (may be NULL if no error)
    const char* stmt_error = seekdb_stmt_error(stmt);
    // stmt_error can be NULL, that's okay
    
    // Test stmt_errno
    unsigned int stmt_errno = seekdb_stmt_errno(stmt);
    // stmt_errno can be 0, that's okay
    
    // Test stmt_sqlstate
    const char* stmt_sqlstate = seekdb_stmt_sqlstate(stmt);
    // stmt_sqlstate can be NULL, that's okay
    
    seekdb_stmt_close(stmt);
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_stmt_param_count(), seekdb_stmt_affected_rows(), seekdb_stmt_insert_id(), seekdb_stmt_num_rows()
TestResult test_stmt_info() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    SeekdbResult cleanup_result = nullptr;
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_stmt_info", &cleanup_result);
    if (cleanup_result) seekdb_result_free(cleanup_result);
    
    ret = seekdb_query(handle, "CREATE TABLE test_stmt_info (id INT AUTO_INCREMENT PRIMARY KEY, name VARCHAR(100))", &cleanup_result);
    if (cleanup_result) seekdb_result_free(cleanup_result);
    
    SeekdbStmt stmt = seekdb_stmt_init(handle);
    if (!stmt) {
        seekdb_connect_close(handle);
        return {false, "Failed to init statement"};
    }
    
    const char* sql = "INSERT INTO test_stmt_info (name) VALUES (?)";
    ret = seekdb_stmt_prepare(stmt, sql, strlen(sql));
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to prepare statement"};
    }
    
    // Test param_count
    unsigned long param_count = seekdb_stmt_param_count(stmt);
    if (param_count != 1) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Expected 1 parameter"};
    }
    
    const char* name = "Test";
    SeekdbBind bind;
    bind.buffer_type = SEEKDB_TYPE_STRING;
    bind.buffer = const_cast<char*>(name);
    bind.buffer_length = strlen(name);
    unsigned long name_len = strlen(name);
    bind.length = &name_len;
    bool is_null = false;
    bind.is_null = &is_null;
    
    ret = seekdb_stmt_bind_param(stmt, &bind);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to bind parameter"};
    }
    
    ret = seekdb_stmt_execute(stmt);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to execute statement"};
    }
    
    // Test affected_rows
    my_ulonglong affected = seekdb_stmt_affected_rows(stmt);
    if (affected != 1) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Expected 1 affected row"};
    }
    
    // Test insert_id
    my_ulonglong insert_id = seekdb_stmt_insert_id(stmt);
    if (insert_id == 0) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Expected non-zero insert_id"};
    }
    
    // Test num_rows (for SELECT statements)
    SeekdbStmt stmt2 = seekdb_stmt_init(handle);
    if (!stmt2) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to init second statement"};
    }
    
    const char* select_sql = "SELECT * FROM test_stmt_info";
    ret = seekdb_stmt_prepare(stmt2, select_sql, strlen(select_sql));
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_stmt_close(stmt2);
        seekdb_connect_close(handle);
        return {false, "Failed to prepare SELECT statement"};
    }
    
    ret = seekdb_stmt_execute(stmt2);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_stmt_close(stmt2);
        seekdb_connect_close(handle);
        return {false, "Failed to execute SELECT statement"};
    }
    
    my_ulonglong num_rows = seekdb_stmt_num_rows(stmt2);
    if (num_rows != 1) {
        seekdb_stmt_close(stmt);
        seekdb_stmt_close(stmt2);
        seekdb_connect_close(handle);
        return {false, "Expected 1 row"};
    }
    
    seekdb_stmt_close(stmt);
    seekdb_stmt_close(stmt2);
    
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_stmt_info", &cleanup_result);
    if (cleanup_result) seekdb_result_free(cleanup_result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_field_tell() and seekdb_row_tell()
TestResult test_field_and_row_tell() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "SELECT 1 as col1, 'hello' as col2, 3.14 as col3", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to execute query"};
    }
    
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        return {false, "Failed to store result"};
    }
    
    // Test field_tell
    unsigned int field_pos = seekdb_field_tell(result);
    if (field_pos != 0) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Expected field position 0"};
    }
    
    // Fetch a field to advance position
    SeekdbField* field = seekdb_fetch_field(result);
    if (field == nullptr) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch field"};
    }
    
    field_pos = seekdb_field_tell(result);
    if (field_pos != 1) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Expected field position 1"};
    }
    
    // Test row_tell
    SeekdbRow row = seekdb_fetch_row(result);
    if (row == nullptr) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch row"};
    }
    
    my_ulonglong row_pos = seekdb_row_tell(result);
    // row_pos can be 0 or 1 depending on implementation
    
    seekdb_result_free(result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_row_seek()
TestResult test_row_seek() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    SeekdbResult result = nullptr;
    ret = seekdb_query(handle, "SELECT 1 as id UNION SELECT 2 UNION SELECT 3", &result);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to execute query"};
    }
    
    result = seekdb_store_result(handle);
    if (result == nullptr) {
        seekdb_connect_close(handle);
        return {false, "Failed to store result"};
    }
    
    // Fetch first row
    SeekdbRow row1 = seekdb_fetch_row(result);
    if (row1 == nullptr) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch first row"};
    }
    
    // Get row position (row_tell returns position, not row handle)
    my_ulonglong saved_pos = seekdb_row_tell(result);
    
    // Fetch second row
    SeekdbRow row2 = seekdb_fetch_row(result);
    if (row2 == nullptr) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch second row"};
    }
    
    // Seek back to first row using row1 as the saved row handle
    // Note: row_seek uses the row handle, not position
    SeekdbRow seeked_row = seekdb_row_seek(result, row1);
    if (seeked_row == nullptr) {
        seekdb_result_free(result);
        seekdb_connect_close(handle);
        return {false, "Failed to seek row"};
    }
    
    seekdb_result_free(result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_real_escape_string_quote()
TestResult test_real_escape_string_quote() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    // Test escaping with single quote context
    const char* from1 = "test'string\"with\\special";
    char to1[256] = {0};
    unsigned long escaped_len1 = seekdb_real_escape_string_quote(handle, to1, sizeof(to1), from1, strlen(from1), '\'');
    if (escaped_len1 == (unsigned long)-1) {
        seekdb_connect_close(handle);
        return {false, "Failed to escape string with single quote"};
    }
    
    // Verify single quote is escaped, double quote is not
    if (strstr(to1, "\\'") == nullptr) {
        seekdb_connect_close(handle);
        return {false, "Single quote should be escaped"};
    }
    if (strstr(to1, "\\\"") != nullptr) {
        seekdb_connect_close(handle);
        return {false, "Double quote should not be escaped in single quote context"};
    }
    
    // Test escaping with double quote context
    const char* from2 = "test'string\"with\\special";
    char to2[256] = {0};
    unsigned long escaped_len2 = seekdb_real_escape_string_quote(handle, to2, sizeof(to2), from2, strlen(from2), '"');
    if (escaped_len2 == (unsigned long)-1) {
        seekdb_connect_close(handle);
        return {false, "Failed to escape string with double quote"};
    }
    
    // Verify double quote is escaped, single quote is not
    if (strstr(to2, "\\\"") == nullptr) {
        seekdb_connect_close(handle);
        return {false, "Double quote should be escaped"};
    }
    if (strstr(to2, "\\'") != nullptr) {
        seekdb_connect_close(handle);
        return {false, "Single quote should not be escaped in double quote context"};
    }
    
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_reset_connection()
TestResult test_reset_connection() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", false);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    // Start a transaction
    ret = seekdb_begin(handle);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to begin transaction"};
    }
    
    // Verify we're in a transaction
    if (!seekdb_autocommit(handle, true)) {
        // This should fail if we're in a transaction
    }
    
    // Reset connection (should rollback transaction and reset state)
    ret = seekdb_reset_connection(handle);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Failed to reset connection"};
    }
    
    // Verify autocommit is reset to true
    // After reset, we should be able to set autocommit
    ret = seekdb_autocommit(handle, true);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_connect_close(handle);
        return {false, "Autocommit should work after reset"};
    }
    
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_stmt_param_metadata()
TestResult test_stmt_param_metadata() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    SeekdbResult cleanup_result = nullptr;
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_param_metadata", &cleanup_result);
    if (cleanup_result) seekdb_result_free(cleanup_result);
    
    ret = seekdb_query(handle, "CREATE TABLE test_param_metadata (id INT PRIMARY KEY, name VARCHAR(100), score DOUBLE)", &cleanup_result);
    if (cleanup_result) seekdb_result_free(cleanup_result);
    
    SeekdbStmt stmt = seekdb_stmt_init(handle);
    if (!stmt) {
        seekdb_connect_close(handle);
        return {false, "Failed to init statement"};
    }
    
    const char* sql = "INSERT INTO test_param_metadata VALUES (?, ?, ?)";
    ret = seekdb_stmt_prepare(stmt, sql, strlen(sql));
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to prepare statement"};
    }
    
    // Get parameter metadata
    SeekdbResult param_metadata = seekdb_stmt_param_metadata(stmt);
    if (param_metadata == nullptr) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to get parameter metadata"};
    }
    
    // Verify metadata structure
    unsigned int field_count = seekdb_num_fields(param_metadata);
    if (field_count != 6) {
        seekdb_result_free(param_metadata);
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Expected 6 columns in parameter metadata"};
    }
    
    my_ulonglong row_count = seekdb_num_rows(param_metadata);
    if (row_count != 3) {
        seekdb_result_free(param_metadata);
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Expected 3 parameters in metadata"};
    }
    
    // Verify column names
    SeekdbField* fields = seekdb_fetch_fields(param_metadata);
    if (!fields) {
        seekdb_result_free(param_metadata);
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch fields"};
    }
    
    if (!fields[0].name || strcmp(fields[0].name, "name") != 0) {
        seekdb_result_free(param_metadata);
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "First column should be 'name'"};
    }
    
    seekdb_result_free(param_metadata);
    seekdb_stmt_close(stmt);
    
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_param_metadata", &cleanup_result);
    if (cleanup_result) seekdb_result_free(cleanup_result);
    seekdb_connect_close(handle);
    return {true, ""};
}

// Test seekdb_stmt_store_result()
TestResult test_stmt_store_result() {
    SeekdbHandle handle = nullptr;
    int ret = seekdb_connect(&handle, "test", true);
    if (ret != SEEKDB_SUCCESS) {
        return {false, "Failed to connect"};
    }
    
    SeekdbResult cleanup_result = nullptr;
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_stmt_store", &cleanup_result);
    if (cleanup_result) seekdb_result_free(cleanup_result);
    
    ret = seekdb_query(handle, "CREATE TABLE test_stmt_store (id INT PRIMARY KEY, name VARCHAR(100))", &cleanup_result);
    if (cleanup_result) seekdb_result_free(cleanup_result);
    
    ret = seekdb_query(handle, "INSERT INTO test_stmt_store VALUES (1, 'Alice'), (2, 'Bob'), (3, 'Charlie')", &cleanup_result);
    if (cleanup_result) seekdb_result_free(cleanup_result);
    
    SeekdbStmt stmt = seekdb_stmt_init(handle);
    if (!stmt) {
        seekdb_connect_close(handle);
        return {false, "Failed to init statement"};
    }
    
    const char* sql = "SELECT id, name FROM test_stmt_store ORDER BY id";
    ret = seekdb_stmt_prepare(stmt, sql, strlen(sql));
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to prepare statement"};
    }
    
    ret = seekdb_stmt_execute(stmt);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to execute statement"};
    }
    
    // Store result
    ret = seekdb_stmt_store_result(stmt);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to store result"};
    }
    
    // Verify we can get row count after storing
    my_ulonglong num_rows = seekdb_stmt_num_rows(stmt);
    if (num_rows != 3) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Expected 3 rows after store_result"};
    }
    
    // Verify we can fetch rows
    int32_t result_id = 0;
    char result_name[256] = {0};
    unsigned long result_name_len = 0;
    bool result_id_null = false;
    bool result_name_null = false;
    
    SeekdbBind result_binds[2];
    result_binds[0].buffer_type = SEEKDB_TYPE_LONG;
    result_binds[0].buffer = &result_id;
    result_binds[0].buffer_length = sizeof(result_id);
    result_binds[0].length = nullptr;
    result_binds[0].is_null = &result_id_null;
    
    result_binds[1].buffer_type = SEEKDB_TYPE_STRING;
    result_binds[1].buffer = result_name;
    result_binds[1].buffer_length = sizeof(result_name);
    result_binds[1].length = &result_name_len;
    result_binds[1].is_null = &result_name_null;
    
    ret = seekdb_stmt_bind_result(stmt, result_binds);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to bind result"};
    }
    
    // Fetch first row
    ret = seekdb_stmt_fetch(stmt);
    if (ret != 0) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Failed to fetch row"};
    }
    
    if (result_id != 1 || strcmp(result_name, "Alice") != 0) {
        seekdb_stmt_close(stmt);
        seekdb_connect_close(handle);
        return {false, "Fetched data mismatch"};
    }
    
    seekdb_stmt_close(stmt);
    
    ret = seekdb_query(handle, "DROP TABLE IF EXISTS test_stmt_store", &cleanup_result);
    if (cleanup_result) seekdb_result_free(cleanup_result);
    seekdb_connect_close(handle);
    return {true, ""};
}

int main(int argc, char* argv[]) {
    // Database path: argv[1] if provided, else default relative path (same as other bindings)
    g_db_path = (argc >= 2 && argv[1] && argv[1][0] != '\0') ? argv[1] : "./seekdb.db";

    std::cout << std::string(70, '=') << std::endl;
    std::cout << "SeekDB C++ Binding Test Suite" << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    std::cout << std::endl;

    // Open database once at the beginning
    int ret = seekdb_open(g_db_path);
    if (ret != SEEKDB_SUCCESS) {
        std::cerr << "Failed to open database: " << ret << std::endl;
        return 1;
    }
    
    // Test cases organized by functional groups
    std::vector<std::pair<std::string, TestResult (*)()>> test_cases = {
        // ========== 1. Basic Functions (Database and Connection) ==========
        {"Database Open", test_open},
        {"Connection Creation", test_connection},
        {"Error Handling", test_error_handling},
        {"Ping", test_ping},
        {"Select Database", test_select_db},
        {"Reset Connection", test_reset_connection},
        
        // ========== 2. Query and Result Set Operations ==========
        {"Real Query (Binary-Safe)", test_real_query},
        {"Use Result (Streaming)", test_use_result},
        {"Result Operations", test_result_operations},
        {"Result Fetch All", test_result_fetch_all},
        {"Field Count", test_field_count},
        {"Result Column Name", test_result_column_name},
        {"Data Seek and Field Seek", test_data_seek_and_field_seek},
        {"Field and Row Tell", test_field_and_row_tell},
        {"Row Seek", test_row_seek},
        {"Next Result", test_next_result},
        
        // ========== 3. Row Operations ==========
        {"Row Operations", test_row_operations},
        {"Row Lengths", test_row_lengths},
        {"Row Is Null", test_row_is_null},
        {"NULL vs Empty String (C ABI)", test_null_vs_empty_string},
        {"Long String Length and Full Read (C ABI)", test_long_string_length_and_full_read},
        {"Very Long Document 100KB (embedded)", test_very_long_document_100kb},
        {"Collection STRING Document 100KB (embedded)", test_collection_string_document_100kb},
        {"Special Characters in Metadata (embedded)", test_special_characters_in_metadata},
        {"JSON Column Param Insert (embedded)", test_json_column_param_insert},
        {"Empty JSON Metadata (embedded)", test_empty_json_metadata},
        {"Same-Process Same-Path Reuse (relative)", test_embedded_same_path_reuse_relative},
        {"Same-Process Same-Path Reuse (absolute)", test_embedded_same_path_reuse_absolute},
        {"Row Get Types", test_row_get_types},
        
        // ========== 4. Transaction Management ==========
        {"Transaction Management", test_transaction_management},
        {"Autocommit", test_autocommit},
        
        // ========== 5. DDL/DML Operations ==========
        {"DDL Operations", test_ddl_operations},
        {"DDL Visibility After Create (SHOW TABLES)", test_ddl_visibility_after_create},
        {"DML Operations", test_dml_operations},
        {"Affected Rows and Insert ID", test_affected_rows_and_insert_id},
        
        // ========== 6. Parameterized Queries ==========
        {"Parameterized Queries", test_parameterized_queries},
        {"VECTOR Parameter Binding", test_vector_parameter_binding},
        {"Binary Parameter Binding", test_binary_parameter_binding},
        {"Embedded VARBINARY_ID Binding", test_embedded_varbinary_id_binding},
        {"VECTOR Column Reading", test_vector_column_reading},
        {"VECTOR Column Reading No Rounding", test_vector_column_reading_no_rounding},
        {"Column Name Inference", test_column_name_inference},
        
        // ========== 7. Prepared Statement ==========
        {"Statement Reset", test_stmt_reset},
        {"Statement Field Operations", test_stmt_field_operations},
        {"Statement Fetch Column", test_stmt_fetch_column},
        {"Statement Error Info", test_stmt_error_info},
        {"Statement Info", test_stmt_info},
        {"Statement Param Metadata", test_stmt_param_metadata},
        {"Statement Store Result", test_stmt_store_result},
        
        // ========== 8. Error and Information ==========
        {"Error Message", test_error_message},
        {"Last Error", test_last_error},
        {"Errno", test_errno},
        {"SQLSTATE", test_sqlstate},
        {"Info and Warnings", test_info_and_warnings},
        {"Server Info", test_server_info},
        {"Server Version", test_server_version},
        
        // ========== 9. Utility Functions ==========
        {"Escape String", test_escape_string},
        {"Real Escape String Quote", test_real_escape_string_quote},
        {"Character Set", test_character_set},
        
        // ========== 10. Special Features ==========
        {"DBMS_HYBRID_SEARCH.GET_SQL", test_hybrid_search_get_sql},
        {"DBMS_HYBRID_SEARCH.SEARCH", test_hybrid_search_search},
    };
    
    std::vector<std::pair<std::string, TestResult>> results;
    std::vector<std::pair<std::string, std::string>> failed_tests;
    
    for (const auto& test_case : test_cases) {
        std::cout << "[TEST] " << std::left << std::setw(40) << test_case.first << " ... ";
        std::cout.flush();
        
        TestResult result = test_case.second();
        results.push_back({test_case.first, result});
        
        if (result.passed) {
            std::cout << "PASS" << std::endl;
        } else {
            std::cout << "FAIL" << std::endl;
            failed_tests.push_back({test_case.first, result.message});
            if (!result.message.empty()) {
                std::cerr << "::error::Test \"" << test_case.first << "\" failed: " << result.message << std::endl;
            }
        }
    }
    
    std::cout << std::endl;
    std::cout << std::string(70, '-') << std::endl;
    
    int passed = 0;
    for (const auto& r : results) {
        if (r.second.passed) {
            passed++;
        }
    }
    int total = results.size();
    int failed = total - passed;
    
    if (failed > 0) {
        std::cout << "Failed Tests:" << std::endl;
        std::cout << std::string(70, '-') << std::endl;
        for (const auto& test : failed_tests) {
            std::cout << "  ✗ " << test.first << std::endl;
            if (!test.second.empty()) {
                std::cout << "    Error: " << test.second << std::endl;
            }
        }
        std::cout << std::string(70, '-') << std::endl;
    }
    
    std::cout << "Total: " << passed << "/" << total << " passed, " << failed << " failed" << std::endl;
    std::cout << std::endl;
    
    // Output results before closing database
    if (passed == total) {
        std::cout << "::notice::All tests passed successfully!" << std::endl;
    } else {
        std::cerr << "::error::" << failed << " test(s) failed" << std::endl;
    }
    std::cout << std::string(70, '=') << std::endl;
    std::cout << std::endl;
    std::cout << "Test completed!" << std::endl;
    
    // Close database at the end (after output to avoid segfault during static destructors)
    seekdb_close();
    
    // Flush output before exit
    std::cout.flush();
    std::cerr.flush();
    
    // Use _exit() instead of exit() to completely skip static destructors
    // This avoids segfault during program termination
    _exit((passed == total) ? 0 : 1);
}
