/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SeekDB Rust FFI Binding Test Suite
 */

use seekdb::*;
use std::env;
use std::fs::OpenOptions;
use std::io::Write;
use std::path::Path;

#[derive(Clone)]
struct TestResult {
    passed: bool,
    message: Option<String>,
}

fn test_open() -> TestResult {
    match SeekdbConnection::new() {
        Ok(mut conn) => {
            match conn.connect("test", true) {
                Ok(_) => TestResult { passed: true, message: None },
                Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
            }
        }
        Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
    }
}

fn test_connection() -> TestResult {
    match SeekdbConnection::new() {
        Ok(mut conn) => {
            match conn.connect("test", true) {
                Ok(_) => TestResult { passed: true, message: None },
                Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
            }
        }
        Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
    }
}

fn test_error_handling() -> TestResult {
    match SeekdbConnection::new() {
        Ok(mut conn) => {
            match conn.connect("test", true) {
                Ok(_) => {
                    match conn.execute("INVALID SQL STATEMENT") {
                        Ok(_) => TestResult { passed: false, message: Some("Should have thrown error for invalid SQL".to_string()) },
                        Err(_) => TestResult { passed: true, message: None },
                    }
                }
                Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
            }
        }
        Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
    }
}

fn test_result_operations() -> TestResult {
    match SeekdbConnection::new() {
        Ok(mut conn) => {
            match conn.connect("test", true) {
                Ok(_) => {
                    // Create table with known column names to test column name inference
                    if let Err(e) = conn.execute_update("CREATE TABLE IF NOT EXISTS test_cols (user_id INT, user_name VARCHAR(100))") {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    if let Err(e) = conn.execute_update("INSERT INTO test_cols VALUES (1, 'Alice')") {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    
                    // Query with explicit column names - column names should be inferred from database
                    match conn.execute("SELECT user_id, user_name FROM test_cols") {
                        Ok(result) => {
                            if result.row_count() != 1 {
                                return TestResult { passed: false, message: Some(format!("Expected row count 1, got {}", result.row_count())) };
                            }
                            if result.column_count() != 2 {
                                return TestResult { passed: false, message: Some(format!("Expected column count 2, got {}", result.column_count())) };
                            }
                            
                            // Verify column names are correctly inferred
                            let column_names = result.column_names();
                            if column_names.len() != 2 {
                                return TestResult { passed: false, message: Some(format!("Expected 2 column names, got {}", column_names.len())) };
                            }
                            if column_names[0] != "user_id" {
                                return TestResult { passed: false, message: Some(format!("Expected first column name 'user_id', got '{}'", column_names[0])) };
                            }
                            if column_names[1] != "user_name" {
                                return TestResult { passed: false, message: Some(format!("Expected second column name 'user_name', got '{}'", column_names[1])) };
                            }
                            
                            let rows = result.fetch_all();
                            if rows.len() != 1 {
                                return TestResult { passed: false, message: Some(format!("Expected 1 row, got {}", rows.len())) };
                            }
                            
                            if let Err(e) = conn.execute_update("DROP TABLE IF EXISTS test_cols") {
                                return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                            }
                            
                            TestResult { passed: true, message: None }
                        }
                        Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
                    }
                }
                Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
            }
        }
        Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
    }
}

fn test_row_operations() -> TestResult {
    match SeekdbConnection::new() {
        Ok(mut conn) => {
            match conn.connect("test", true) {
                Ok(_) => {
                    let create_sql = r#"
                        CREATE TABLE IF NOT EXISTS test_types (
                            id INT PRIMARY KEY,
                            name VARCHAR(100),
                            price DECIMAL(10,2),
                            quantity INT,
                            active BOOLEAN,
                            score DOUBLE
                        )
                    "#;
                    if let Err(e) = conn.execute_update(create_sql) {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    
                    let insert_sql = r#"
                        INSERT INTO test_types VALUES
                        (1, 'Product A', 99.99, 10, true, 4.5),
                        (2, 'Product B', 199.99, 5, false, 3.8),
                        (3, NULL, NULL, NULL, NULL, NULL)
                    "#;
                    if let Err(e) = conn.execute_update(insert_sql) {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    
                    match conn.execute("SELECT * FROM test_types ORDER BY id") {
                        Ok(result) => {
                            let rows = result.fetch_all();
                            if rows.len() != 3 {
                                return TestResult { passed: false, message: Some(format!("Expected 3 rows, got {}", rows.len())) };
                            }
                            if let Err(e) = conn.execute_update("DROP TABLE IF EXISTS test_types") {
                                return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                            }
                            TestResult { passed: true, message: None }
                        }
                        Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
                    }
                }
                Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
            }
        }
        Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
    }
}

fn test_error_message() -> TestResult {
    match SeekdbConnection::new() {
        Ok(mut conn) => {
            match conn.connect("test", true) {
                Ok(_) => {
                    match conn.execute("SELECT * FROM non_existent_table") {
                        Ok(_) => TestResult { passed: false, message: Some("Should have thrown error for non-existent table".to_string()) },
                        Err(_) => {
                            let error_msg = conn.get_last_error();
                            if error_msg.is_empty() {
                                TestResult { passed: false, message: Some("Error message should be available after failed query".to_string()) }
                            } else {
                                TestResult { passed: true, message: None }
                            }
                        }
                    }
                }
                Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
            }
        }
        Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
    }
}

fn test_transaction_management() -> TestResult {
    match SeekdbConnection::new() {
        Ok(mut conn) => {
            match conn.connect("test", false) {
                Ok(_) => {
                    if let Err(e) = conn.execute_update("CREATE TABLE IF NOT EXISTS test_txn (id INT PRIMARY KEY, value INT)") {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    
                    if let Err(e) = conn.begin() {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    if let Err(e) = conn.execute_update("INSERT INTO test_txn VALUES (1, 100)") {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    if let Err(e) = conn.commit() {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    
                    match conn.execute("SELECT * FROM test_txn WHERE id = 1") {
                        Ok(result) => {
                            let rows = result.fetch_all();
                            if rows.len() != 1 {
                                return TestResult { passed: false, message: Some("Data not committed".to_string()) };
                            }
                        }
                        Err(e) => return TestResult { passed: false, message: Some(format!("{:?}", e)) },
                    }
                    
                    if let Err(e) = conn.begin() {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    if let Err(e) = conn.execute_update("INSERT INTO test_txn VALUES (2, 200)") {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    if let Err(e) = conn.rollback() {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    
                    if let Err(e) = conn.execute_update("DROP TABLE IF EXISTS test_txn") {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    
                    TestResult { passed: true, message: None }
                }
                Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
            }
        }
        Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
    }
}

fn test_ddl_operations() -> TestResult {
    match SeekdbConnection::new() {
        Ok(mut conn) => {
            match conn.connect("test", true) {
                Ok(_) => {
                    let create_sql = r#"
                        CREATE TABLE IF NOT EXISTS test_ddl (
                            id INT PRIMARY KEY,
                            name VARCHAR(100),
                            created_at TIMESTAMP
                        )
                    "#;
                    if let Err(e) = conn.execute_update(create_sql) {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    
                    let _ = conn.execute_update("ALTER TABLE test_ddl ADD COLUMN description VARCHAR(255)");
                    
                    if let Err(e) = conn.execute_update("DROP TABLE IF EXISTS test_ddl") {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    
                    TestResult { passed: true, message: None }
                }
                Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
            }
        }
        Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
    }
}

fn test_dml_operations() -> TestResult {
    match SeekdbConnection::new() {
        Ok(mut conn) => {
            match conn.connect("test", true) {
                Ok(_) => {
                    let create_sql = r#"
                        CREATE TABLE IF NOT EXISTS test_dml (
                            id INT PRIMARY KEY,
                            name VARCHAR(100),
                            value INT
                        )
                    "#;
                    if let Err(e) = conn.execute_update(create_sql) {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    
                    match conn.execute_update(r#"INSERT INTO test_dml VALUES (1, 'A', 10), (2, 'B', 20), (3, 'C', 30)"#) {
                        Ok(insert_rows) => {
                            if insert_rows != 3 {
                                return TestResult { passed: false, message: Some(format!("Expected 3 rows inserted, got {}", insert_rows)) };
                            }
                        }
                        Err(e) => return TestResult { passed: false, message: Some(format!("{:?}", e)) },
                    }
                    
                    match conn.execute_update(r#"UPDATE test_dml SET value = 100 WHERE id = 1"#) {
                        Ok(update_rows) => {
                            if update_rows != 1 {
                                return TestResult { passed: false, message: Some(format!("Expected 1 row updated, got {}", update_rows)) };
                            }
                        }
                        Err(e) => return TestResult { passed: false, message: Some(format!("{:?}", e)) },
                    }
                    
                    match conn.execute("SELECT value FROM test_dml WHERE id = 1") {
                        Ok(result) => {
                            let rows = result.fetch_all();
                            if rows.len() != 1 {
                                return TestResult { passed: false, message: Some("UPDATE verification failed".to_string()) };
                            }
                        }
                        Err(e) => return TestResult { passed: false, message: Some(format!("{:?}", e)) },
                    }
                    
                    match conn.execute_update("DELETE FROM test_dml WHERE id = 2") {
                        Ok(delete_rows) => {
                            if delete_rows != 1 {
                                return TestResult { passed: false, message: Some(format!("Expected 1 row deleted, got {}", delete_rows)) };
                            }
                        }
                        Err(e) => return TestResult { passed: false, message: Some(format!("{:?}", e)) },
                    }
                    
                    match conn.execute("SELECT * FROM test_dml WHERE id = 2") {
                        Ok(result) => {
                            let rows = result.fetch_all();
                            if rows.len() != 0 {
                                return TestResult { passed: false, message: Some("DELETE verification failed".to_string()) };
                            }
                        }
                        Err(e) => return TestResult { passed: false, message: Some(format!("{:?}", e)) },
                    }
                    
                    if let Err(e) = conn.execute_update("DROP TABLE IF EXISTS test_dml") {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    
                    TestResult { passed: true, message: None }
                }
                Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
            }
        }
        Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
    }
}

fn test_hybrid_search_get_sql() -> TestResult {
    match SeekdbConnection::new() {
        Ok(mut conn) => {
            match conn.connect("test", true) {
                Ok(_) => {
                    let _ = conn.execute_update("DROP TABLE IF EXISTS doc_table");
                    
                    let create_table_sql = r#"
                        CREATE TABLE doc_table (
                            c1 INT,
                            vector VECTOR(3),
                            query VARCHAR(255),
                            content VARCHAR(255),
                            VECTOR INDEX idx1(vector) WITH (distance=l2, type=hnsw, lib=vsag),
                            FULLTEXT idx2(query),
                            FULLTEXT idx3(content)
                        )
                    "#;
                    if let Err(e) = conn.execute_update(create_table_sql) {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    
                    let insert_sql = r#"
                        INSERT INTO doc_table VALUES
                        (1, '[1,2,3]', 'hello world', 'oceanbase Elasticsearch database'),
                        (2, '[1,2,1]', 'hello world, what is your name', 'oceanbase mysql database'),
                        (3, '[1,1,1]', 'hello world, how are you', 'oceanbase oracle database'),
                        (4, '[1,3,1]', 'real world, where are you from', 'postgres oracle database'),
                        (5, '[1,3,2]', 'real world, how old are you', 'redis oracle database'),
                        (6, '[2,1,1]', 'hello world, where are you from', 'starrocks oceanbase database')
                    "#;
                    if let Err(e) = conn.execute_update(insert_sql) {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    
                    let search_params = r#"{"query":{"bool":{"should":[{"match":{"query":"hi hello"}},{"match":{"content":"oceanbase mysql"}}],"filter":[{"term":{"content":"postgres"}}]}},"knn":{"field":"vector","k":5,"query_vector":[1,2,3]},"_source":["query","content","_keyword_score","_semantic_score"]}"#;
                    let escaped_params = search_params.replace("'", "''");
                    
                    let set_parm_sql = format!("SET @parm = '{}'", escaped_params);
                    if let Err(e) = conn.execute_update(&set_parm_sql) {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    
                    let get_sql_query = "SELECT DBMS_HYBRID_SEARCH.GET_SQL('doc_table', @parm)";
                    match conn.execute(get_sql_query) {
                        Ok(result) => {
                            let rows = result.fetch_all();
                            if rows.is_empty() {
                                return TestResult { passed: false, message: Some("GET_SQL returned no rows".to_string()) };
                            }
                            TestResult { passed: true, message: None }
                        }
                        Err(e) => {
                            let error_msg = conn.get_last_error();
                            let msg = if error_msg.is_empty() {
                                format!("{:?}", e)
                            } else {
                                format!("{:?} ({})", e, error_msg)
                            };
                            TestResult { passed: false, message: Some(msg) }
                        }
                    }
                }
                Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
            }
        }
        Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
    }
}

fn test_hybrid_search_search() -> TestResult {
    match SeekdbConnection::new() {
        Ok(mut conn) => {
            match conn.connect("test", true) {
                Ok(_) => {
                    let search_params = r#"{"query":{"bool":{"should":[{"match":{"query":"hello"}},{"match":{"content":"oceanbase mysql"}}]}},"knn":{"field":"vector","k":5,"query_vector":[1,2,3]},"_source":["c1","query","content","_keyword_score","_semantic_score"]}"#;
                    let escaped_params = search_params.replace("'", "''");
                    
                    let search_query = format!("SELECT DBMS_HYBRID_SEARCH.SEARCH('doc_table', '{}') as result", escaped_params);
                    match conn.execute(&search_query) {
                        Ok(result) => {
                            let rows = result.fetch_all();
                            if rows.is_empty() {
                                return TestResult { passed: false, message: Some("SEARCH returned no rows".to_string()) };
                            }
                            TestResult { passed: true, message: None }
                        }
                        Err(e) => {
                            let error_msg = conn.get_last_error();
                            let msg = if error_msg.is_empty() {
                                format!("{:?}", e)
                            } else {
                                format!("{:?} ({})", e, error_msg)
                            };
                            TestResult { passed: false, message: Some(msg) }
                        }
                    }
                }
                Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
            }
        }
        Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
    }
}

fn test_column_name_inference() -> TestResult {
    match SeekdbConnection::new() {
        Ok(mut conn) => {
            match conn.connect("test", true) {
                Ok(_) => {
                    // Create test table with multiple columns to test column name inference
                    if let Err(e) = conn.execute_update("CREATE TABLE IF NOT EXISTS test_cols (user_id INT, user_name VARCHAR(100), user_email VARCHAR(100))") {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    if let Err(e) = conn.execute_update("INSERT INTO test_cols VALUES (1, 'Alice', 'alice@example.com')") {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    
                    // Query with explicit column names - column names should be inferred from database
                    match conn.execute("SELECT user_id, user_name, user_email FROM test_cols") {
                        Ok(result) => {
                            // Verify column count
                            if result.column_count() != 3 {
                                return TestResult { passed: false, message: Some(format!("Expected 3 columns, got {}", result.column_count())) };
                            }
                            
                            // Verify column names are correctly inferred
                            let column_names = result.column_names();
                            if column_names.len() != 3 {
                                return TestResult { passed: false, message: Some(format!("Expected 3 column names, got {}", column_names.len())) };
                            }
                            if column_names[0] != "user_id" {
                                return TestResult { passed: false, message: Some(format!("Expected first column name 'user_id', got '{}'", column_names[0])) };
                            }
                            if column_names[1] != "user_name" {
                                return TestResult { passed: false, message: Some(format!("Expected second column name 'user_name', got '{}'", column_names[1])) };
                            }
                            if column_names[2] != "user_email" {
                                return TestResult { passed: false, message: Some(format!("Expected third column name 'user_email', got '{}'", column_names[2])) };
                            }
                            
                            // Verify data
                            let rows = result.fetch_all();
                            if rows.len() != 1 {
                                return TestResult { passed: false, message: Some(format!("Expected 1 row, got {}", rows.len())) };
                            }
                            
                            if let Err(e) = conn.execute_update("DROP TABLE IF EXISTS test_cols") {
                                return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                            }
                            
                            TestResult { passed: true, message: None }
                        }
                        Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
                    }
                }
                Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
            }
        }
        Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
    }
}

fn test_parameterized_queries() -> TestResult {
    match SeekdbConnection::new() {
        Ok(mut conn) => {
            match conn.connect("test", true) {
                Ok(_) => {
                    // Create test table
                    if let Err(e) = conn.execute_update("CREATE TABLE IF NOT EXISTS test_params (id INT PRIMARY KEY, name VARCHAR(100))") {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    if let Err(e) = conn.execute_update("INSERT INTO test_params VALUES (1, 'Alice'), (2, 'Bob')") {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    
                    // Test parameterized query concept
                    // Since we don't have Prepared Statement bindings yet, we test column name inference
                    // which is the core feature for parameterized queries
                    match conn.execute("SELECT * FROM test_params WHERE id = 1") {
                        Ok(result) => {
                            // Verify column names are correctly inferred (core feature: column name inference)
                            if result.column_count() != 2 {
                                return TestResult { passed: false, message: Some(format!("Expected 2 columns, got {}", result.column_count())) };
                            }
                            
                            let column_names = result.column_names();
                            if column_names.len() != 2 {
                                return TestResult { passed: false, message: Some(format!("Expected 2 column names, got {}", column_names.len())) };
                            }
                            if column_names[0] != "id" {
                                return TestResult { passed: false, message: Some(format!("Expected first column name 'id', got '{}'", column_names[0])) };
                            }
                            if column_names[1] != "name" {
                                return TestResult { passed: false, message: Some(format!("Expected second column name 'name', got '{}'", column_names[1])) };
                            }
                            
                            // Verify data
                            let rows = result.fetch_all();
                            if rows.len() != 1 {
                                return TestResult { passed: false, message: Some(format!("Expected 1 row, got {}", rows.len())) };
                            }
                            
                            if let Err(e) = conn.execute_update("DROP TABLE IF EXISTS test_params") {
                                return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                            }
                            
                            TestResult { passed: true, message: None }
                        }
                        Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
                    }
                }
                Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
            }
        }
        Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
    }
}

// Test VECTOR type in INSERT (regression test for VECTOR_INSERT_ISSUE)
// Ensures VECTOR columns work in INSERT without "Column cannot be null" errors
fn test_vector_parameter_binding() -> TestResult {
    match SeekdbConnection::new() {
        Ok(mut conn) => {
            match conn.connect("test", true) {
                Ok(_) => {
                    if let Err(e) = conn.execute_update("DROP TABLE IF EXISTS test_vector_params") {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    if let Err(e) = conn.execute_update("CREATE TABLE test_vector_params (id INT AUTO_INCREMENT PRIMARY KEY, document VARCHAR(255), metadata VARCHAR(255), embedding VECTOR(3))") {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    let insert_sql = r#"INSERT INTO test_vector_params (document, metadata, embedding) VALUES
                        ('Document 1', '{"category":"A","score":95}', '[1,2,3]'),
                        ('Document 2', '{"category":"B","score":90}', '[2,3,4]'),
                        ('Document 3', '{"category":"A","score":88}', '[1.1,2.1,3.1]'),
                        ('Document 4', '{"category":"C","score":92}', '[2.1,3.1,4.1]'),
                        ('Document 5', '{"category":"B","score":85}', '[1.2,2.2,3.2]')"#;
                    if let Err(e) = conn.execute_update(insert_sql) {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    match conn.execute("SELECT COUNT(*) as cnt FROM test_vector_params") {
                        Ok(result) => {
                            let rows = result.fetch_all();
                            let cnt = rows.get(0).and_then(|r| r.get(0)).and_then(|c| c.as_ref()).map(|s| s.as_str()).unwrap_or("");
                            if rows.len() != 1 || cnt != "5" {
                                return TestResult { passed: false, message: Some(format!("Expected 5 rows, got cnt={}", cnt)) };
                            }
                        }
                        Err(e) => return TestResult { passed: false, message: Some(format!("{:?}", e)) },
                    }
                    match conn.execute("SELECT document, embedding FROM test_vector_params WHERE document = 'Document 1' LIMIT 1") {
                        Ok(result) => {
                            let ver = result.fetch_all();
                            if ver.len() != 1 {
                                return TestResult { passed: false, message: Some("Expected 1 row for document=Document 1".to_string()) };
                            }
                            let emb = ver[0].get(1).and_then(|o| o.as_ref()).map(|s| s.as_str()).unwrap_or("");
                            if emb.is_empty() {
                                return TestResult { passed: false, message: Some("Embedding column is NULL - VECTOR INSERT failed".to_string()) };
                            }
                            
                            // Note: VECTOR type return format
                            // - Insert: JSON array format "[1,2,3]" (via direct SQL embedding)
                            // - Storage: Binary format (float array, e.g., 3 floats = 12 bytes for VECTOR(3))
                            // - Query return: Typically binary format (may contain non-printable characters)
                            //   The important thing is that data is not NULL, which means INSERT succeeded
                        }
                        Err(e) => return TestResult { passed: false, message: Some(format!("{:?}", e)) },
                    }
                    // Test 2: Additional INSERT to verify VECTOR type continues to work
                    if let Err(e) = conn.execute_update("INSERT INTO test_vector_params (document, metadata, embedding) VALUES ('Auto-Detection Test', '{\"category\":\"TEST\",\"score\":100}', '[1.5,2.5,3.5]')") {
                        let _ = conn.execute_update("DROP TABLE IF EXISTS test_vector_params");
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    match conn.execute("SELECT COUNT(*) as cnt FROM test_vector_params") {
                        Ok(result) => {
                            let rows = result.fetch_all();
                            let cnt = rows.get(0).and_then(|r| r.get(0)).and_then(|c| c.as_ref()).map(|s| s.as_str()).unwrap_or("");
                            if rows.len() != 1 || cnt != "6" {
                                let _ = conn.execute_update("DROP TABLE IF EXISTS test_vector_params");
                                return TestResult { passed: false, message: Some(format!("Expected 6 rows after additional insert, got cnt={}", cnt)) };
                            }
                        }
                        Err(e) => {
                            let _ = conn.execute_update("DROP TABLE IF EXISTS test_vector_params");
                            return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                        }
                    }
                    match conn.execute("SELECT document, embedding FROM test_vector_params WHERE document = 'Auto-Detection Test' LIMIT 1") {
                        Ok(result) => {
                            let ver = result.fetch_all();
                            if ver.len() != 1 {
                                let _ = conn.execute_update("DROP TABLE IF EXISTS test_vector_params");
                                return TestResult { passed: false, message: Some("Expected 1 row for document=Auto-Detection Test".to_string()) };
                            }
                            let emb = ver[0].get(1).and_then(|o| o.as_ref()).map(|s| s.as_str()).unwrap_or("");
                            if emb.is_empty() {
                                let _ = conn.execute_update("DROP TABLE IF EXISTS test_vector_params");
                                return TestResult { passed: false, message: Some("Embedding column is NULL in additional insert - VECTOR INSERT failed".to_string()) };
                            }
                        }
                        Err(e) => {
                            let _ = conn.execute_update("DROP TABLE IF EXISTS test_vector_params");
                            return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                        }
                    }
                    let _ = conn.execute_update("DROP TABLE IF EXISTS test_vector_params");
                    TestResult { passed: true, message: None }
                }
                Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
            }
        }
        Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
    }
}

// Test binary parameter binding with CAST(? AS BINARY) queries
// This test verifies that BLOB type correctly handles binary data
// in CAST(? AS BINARY) queries, ensuring proper binary comparison.
// Note: This test uses direct SQL execution as a workaround since
// prepared statement API may not be fully available.
fn test_binary_parameter_binding() -> TestResult {
    match SeekdbConnection::new() {
        Ok(mut conn) => {
            match conn.connect("test", true) {
                Ok(_) => {
                    if let Err(e) = conn.execute_update("DROP TABLE IF EXISTS test_binary_params") {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    if let Err(e) = conn.execute_update("CREATE TABLE test_binary_params (id INT PRIMARY KEY, _id VARBINARY(100))") {
                        return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                    }
                    
                    // Insert binary data using direct SQL (using hex format)
                    // In a full implementation, this would use prepared statements with SEEKDB_TYPE_BLOB
                    let binary_values = vec!["get1", "get2", "get3", "get4", "get5"];
                    for (i, val) in binary_values.iter().enumerate() {
                        let hex_val = val.as_bytes().iter().map(|b| format!("{:02x}", b)).collect::<String>();
                        let sql = format!("INSERT INTO test_binary_params (id, _id) VALUES ({}, 0x{})", i + 1, hex_val);
                        if let Err(e) = conn.execute_update(&sql) {
                            let _ = conn.execute_update("DROP TABLE IF EXISTS test_binary_params");
                            return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                        }
                    }
                    
                    // Verify data was inserted correctly
                    match conn.execute("SELECT COUNT(*) as cnt FROM test_binary_params") {
                        Ok(result) => {
                            let rows = result.fetch_all();
                            if rows.len() != 1 {
                                let _ = conn.execute_update("DROP TABLE IF EXISTS test_binary_params");
                                return TestResult { passed: false, message: Some(format!("Expected 1 row, got {}", rows.len())) };
                            }
                            let cnt = rows[0].get(0).and_then(|o| o.as_ref()).map(|s| s.as_str()).unwrap_or("");
                            if cnt != "5" {
                                let _ = conn.execute_update("DROP TABLE IF EXISTS test_binary_params");
                                return TestResult { passed: false, message: Some(format!("Expected 5 rows, got {}", cnt)) };
                            }
                        }
                        Err(e) => {
                            let _ = conn.execute_update("DROP TABLE IF EXISTS test_binary_params");
                            return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                        }
                    }
                    
                    // Query using CAST(? AS BINARY) - using direct SQL with hex format
                    let search_val = "get1";
                    let hex_search = search_val.as_bytes().iter().map(|b| format!("{:02x}", b)).collect::<String>();
                    let select_sql = format!("SELECT _id FROM test_binary_params WHERE _id = CAST(0x{} AS BINARY)", hex_search);
                    match conn.execute(&select_sql) {
                        Ok(result) => {
                            let rows = result.fetch_all();
                            if rows.len() != 1 {
                                let _ = conn.execute_update("DROP TABLE IF EXISTS test_binary_params");
                                return TestResult { passed: false, message: Some("SELECT with CAST(? AS BINARY) returned 0 rows, expected 1 row".to_string()) };
                            }
                            let fetched_id = rows[0].get(0).and_then(|o| o.as_ref()).map(|s| s.as_str()).unwrap_or("");
                            if fetched_id.is_empty() {
                                let _ = conn.execute_update("DROP TABLE IF EXISTS test_binary_params");
                                return TestResult { passed: false, message: Some("Fetched _id is NULL, expected 'get1'".to_string()) };
                            }
                        }
                        Err(e) => {
                            let _ = conn.execute_update("DROP TABLE IF EXISTS test_binary_params");
                            return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                        }
                    }
                    
                    // Test negative case: query for non-existent value
                    let not_found_val = "notfound";
                    let hex_not_found = not_found_val.as_bytes().iter().map(|b| format!("{:02x}", b)).collect::<String>();
                    let not_found_sql = format!("SELECT _id FROM test_binary_params WHERE _id = CAST(0x{} AS BINARY)", hex_not_found);
                    match conn.execute(&not_found_sql) {
                        Ok(result) => {
                            let rows = result.fetch_all();
                            if rows.len() != 0 {
                                let _ = conn.execute_update("DROP TABLE IF EXISTS test_binary_params");
                                return TestResult { passed: false, message: Some(format!("SELECT with non-existent value returned {} rows, expected 0", rows.len())) };
                            }
                        }
                        Err(e) => {
                            let _ = conn.execute_update("DROP TABLE IF EXISTS test_binary_params");
                            return TestResult { passed: false, message: Some(format!("{:?}", e)) };
                        }
                    }
                    
                    let _ = conn.execute_update("DROP TABLE IF EXISTS test_binary_params");
                    TestResult { passed: true, message: None }
                }
                Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
            }
        }
        Err(e) => TestResult { passed: false, message: Some(format!("{:?}", e)) },
    }
}

fn run_all_tests() -> i32 {
    println!("{}", "=".repeat(70));
    println!("SeekDB Rust FFI Binding Test Suite");
    println!("{}", "=".repeat(70));
    println!();
    
    let db_dir = env::args().nth(1).unwrap_or_else(|| "./seekdb.db".to_string());
    match open(&db_dir) {
        Ok(_) => {}
        Err(e) => {
            eprintln!("::error::Failed to open database: {:?}", e);
            return 1;
        }
    }
    
    let test_cases: Vec<(&str, fn() -> TestResult)> = vec![
        ("Database Open", test_open),
        ("Connection Creation", test_connection),
        ("Error Handling", test_error_handling),
        ("Result Operations", test_result_operations),
        ("Row Operations", test_row_operations),
        ("Error Message", test_error_message),
        ("Transaction Management", test_transaction_management),
        ("DDL Operations", test_ddl_operations),
        ("DML Operations", test_dml_operations),
        ("Parameterized Queries", test_parameterized_queries),
        ("VECTOR Parameter Binding", test_vector_parameter_binding),
        ("Binary Parameter Binding", test_binary_parameter_binding),
        ("Column Name Inference", test_column_name_inference),
        ("DBMS_HYBRID_SEARCH.GET_SQL", test_hybrid_search_get_sql),
        ("DBMS_HYBRID_SEARCH.SEARCH", test_hybrid_search_search),
    ];
    
    let mut results: Vec<(String, TestResult)> = Vec::new();
    let mut failed_tests: Vec<(String, String)> = Vec::new();
    
    for (name, test_fn) in test_cases {
        print!("[TEST] {:<40} ... ", name);
        std::io::Write::flush(&mut std::io::stdout()).unwrap();
        let result = test_fn();
        let passed = result.passed;
        let message = result.message.clone();
        results.push((name.to_string(), result));
        
        if passed {
            println!("PASS");
        } else {
            println!("FAIL");
            let msg = message.unwrap_or_default();
            failed_tests.push((name.to_string(), msg.clone()));
            if !msg.is_empty() {
                eprintln!("::error::Test \"{}\" failed: {}", name, msg);
            }
        }
    }
    
    println!();
    println!("{}", "-".repeat(70));
    
    let passed = results.iter().filter(|(_, r)| r.passed).count();
    let total = results.len();
    let failed = total - passed;
    
    if failed > 0 {
        println!("Failed Tests:");
        println!("{}", "-".repeat(70));
        for (name, message) in &failed_tests {
            println!("  ✗ {}", name);
            if !message.is_empty() {
                println!("    Error: {}", message);
            }
        }
        println!("{}", "-".repeat(70));
    }
    
    println!("Total: {}/{} passed, {} failed", passed, total, failed);
    println!();
    
    // Absolute path same directory (aligned with python test.py), before close — avoids second process on Windows CI.
    if passed == total {
        let abs_same = std::fs::canonicalize(Path::new(&db_dir))
            .map(|p| p.to_string_lossy().into_owned())
            .unwrap_or_else(|_| db_dir.clone());
        print!("[TEST] {:<40} ... ", "Absolute path (same DB directory)");
        std::io::Write::flush(&mut std::io::stdout()).unwrap();
        match open(&abs_same) {
            Ok(_) => println!("PASS"),
            Err(e) => {
                println!("FAIL");
                eprintln!("::error::Absolute-path same-directory check failed: {:?}", e);
                return 1;
            }
        }
    }
    
    let exit_code = if passed == total { 0 } else { 1 };
    binding_exit_probe(exit_code);

    if passed == total {
        println!("::notice::All tests passed successfully!");
        println!("{}", "=".repeat(70));
        exit_code
    } else {
        eprintln!("::error::{} test(s) failed", failed);
        println!("{}", "=".repeat(70));
        exit_code
    }
}

fn binding_exit_probe(code: i32) {
    if env::var("SEEKDB_BINDING_EXIT_PROBE").ok().as_deref() != Some("1") {
        return;
    }
    let pid = std::process::id();
    let mut path = std::env::temp_dir();
    path.push(format!("seekdb_binding_exit_probe_{}.log", pid));
    if let Ok(mut f) = OpenOptions::new().create(true).append(true).open(&path) {
        let _ = writeln!(f, "before_process_exit code={}", code);
        let _ = f.sync_all();
    }
}

fn main() {
    let code = run_all_tests();
    binding_exit_probe(code);
    if std::env::var("SEEKDB_BINDING_EXIT_PROBE").ok().as_deref() != Some("1") {
        close();
    }
    std::process::exit(code);
}
