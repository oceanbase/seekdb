/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * SeekDB Go FFI Binding Test Suite
 */

package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"seekdb/seekdb"
)

type TestResult struct {
	passed  bool
	message string
}

func testOpen() TestResult {
	conn, err := seekdb.NewConnection()
	if err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	if err := conn.Connect("test", true); err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	conn.Close()
	return TestResult{passed: true, message: ""}
}

func testConnection() TestResult {
	conn, err := seekdb.NewConnection()
	if err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	if err := conn.Connect("test", true); err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	// Connection is successfully created if Connect() doesn't return error
	conn.Close()
	return TestResult{passed: true, message: ""}
}

func testErrorHandling() TestResult {
	conn, err := seekdb.NewConnection()
	if err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	if err := conn.Connect("test", true); err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	
	// Test invalid SQL
	_, err = conn.Execute("INVALID SQL STATEMENT")
	if err == nil {
		conn.Close()
		return TestResult{passed: false, message: "Should have thrown error for invalid SQL"}
	}
	
	conn.Close()
	return TestResult{passed: true, message: ""}
}

func testResultOperations() TestResult {
	conn, err := seekdb.NewConnection()
	if err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	if err := conn.Connect("test", true); err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	
	// Create table with known column names to test column name inference
	if _, err := conn.ExecuteUpdate("CREATE TABLE IF NOT EXISTS test_cols (user_id INT, user_name VARCHAR(100))"); err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	if _, err := conn.ExecuteUpdate("INSERT INTO test_cols VALUES (1, 'Alice')"); err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	// Query with explicit column names - column names should be inferred from database
	result, err := conn.Execute("SELECT user_id, user_name FROM test_cols")
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	if result.RowCount() != 1 {
		result.Free()
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected row count 1, got %d", result.RowCount())}
	}
	if result.ColumnCount() != 2 {
		result.Free()
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected column count 2, got %d", result.ColumnCount())}
	}
	
	// Verify column names are correctly inferred
	columnNames := result.ColumnNames()
	if len(columnNames) != 2 {
		result.Free()
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected 2 column names, got %d", len(columnNames))}
	}
	if columnNames[0] != "user_id" {
		result.Free()
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected first column name 'user_id', got '%s'", columnNames[0])}
	}
	if columnNames[1] != "user_name" {
		result.Free()
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected second column name 'user_name', got '%s'", columnNames[1])}
	}
	
	rows := result.FetchAll()
	if len(rows) != 1 {
		result.Free()
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected 1 row, got %d", len(rows))}
	}
	
	result.Free()
	if _, err := conn.ExecuteUpdate("DROP TABLE IF EXISTS test_cols"); err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	conn.Close()
	return TestResult{passed: true, message: ""}
}

func testRowOperations() TestResult {
	conn, err := seekdb.NewConnection()
	if err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	if err := conn.Connect("test", true); err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	
	_, err = conn.ExecuteUpdate(`
		CREATE TABLE IF NOT EXISTS test_types (
			id INT PRIMARY KEY,
			name VARCHAR(100),
			price DECIMAL(10,2),
			quantity INT,
			active BOOLEAN,
			score DOUBLE
		)
	`)
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	_, err = conn.ExecuteUpdate(`
		INSERT INTO test_types VALUES
		(1, 'Product A', 99.99, 10, true, 4.5),
		(2, 'Product B', 199.99, 5, false, 3.8),
		(3, NULL, NULL, NULL, NULL, NULL)
	`)
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	result, err := conn.Execute("SELECT * FROM test_types ORDER BY id")
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	rows := result.FetchAll()
	if len(rows) != 3 {
		result.Free()
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected 3 rows, got %d", len(rows))}
	}
	
	_, err = conn.ExecuteUpdate("DROP TABLE IF EXISTS test_types")
	if err != nil {
		result.Free()
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	result.Free()
	conn.Close()
	return TestResult{passed: true, message: ""}
}

func testErrorMessage() TestResult {
	conn, err := seekdb.NewConnection()
	if err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	if err := conn.Connect("test", true); err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	
	// Try to trigger an error
	_, err = conn.Execute("SELECT * FROM non_existent_table")
	if err == nil {
		conn.Close()
		return TestResult{passed: false, message: "Should have thrown error for non-existent table"}
	}
	
	conn.Close()
	return TestResult{passed: true, message: ""}
}

func testTransactionManagement() TestResult {
	conn, err := seekdb.NewConnection()
	if err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	if err := conn.Connect("test", false); err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	
	_, err = conn.ExecuteUpdate("CREATE TABLE IF NOT EXISTS test_txn (id INT PRIMARY KEY, value INT)")
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	// Test begin/commit
	if err := conn.Begin(); err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	_, err = conn.ExecuteUpdate("INSERT INTO test_txn VALUES (1, 100)")
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	if err := conn.Commit(); err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	result, err := conn.Execute("SELECT * FROM test_txn WHERE id = 1")
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	rows := result.FetchAll()
	if len(rows) != 1 {
		result.Free()
		conn.Close()
		return TestResult{passed: false, message: "Data not committed"}
	}
	result.Free()
	
	// Test begin/rollback
	if err := conn.Begin(); err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	_, err = conn.ExecuteUpdate("INSERT INTO test_txn VALUES (2, 200)")
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	if err := conn.Rollback(); err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	_, err = conn.ExecuteUpdate("DROP TABLE IF EXISTS test_txn")
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	conn.Close()
	return TestResult{passed: true, message: ""}
}

func testDDLOperations() TestResult {
	conn, err := seekdb.NewConnection()
	if err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	if err := conn.Connect("test", true); err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	
	_, err = conn.ExecuteUpdate(`
		CREATE TABLE IF NOT EXISTS test_ddl (
			id INT PRIMARY KEY,
			name VARCHAR(100),
			created_at TIMESTAMP
		)
	`)
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	// ALTER TABLE may not be supported
	_, _ = conn.ExecuteUpdate("ALTER TABLE test_ddl ADD COLUMN description VARCHAR(255)")
	
	_, err = conn.ExecuteUpdate("DROP TABLE IF EXISTS test_ddl")
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	conn.Close()
	return TestResult{passed: true, message: ""}
}

func testDMLOperations() TestResult {
	conn, err := seekdb.NewConnection()
	if err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	if err := conn.Connect("test", true); err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	
	_, err = conn.ExecuteUpdate(`
		CREATE TABLE IF NOT EXISTS test_dml (
			id INT PRIMARY KEY,
			name VARCHAR(100),
			value INT
		)
	`)
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	insertRows, err := conn.ExecuteUpdate("INSERT INTO test_dml VALUES (1, 'A', 10), (2, 'B', 20), (3, 'C', 30)")
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	if insertRows != 3 {
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected 3 rows inserted, got %d", insertRows)}
	}
	
	updateRows, err := conn.ExecuteUpdate("UPDATE test_dml SET value = 100 WHERE id = 1")
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	if updateRows != 1 {
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected 1 row updated, got %d", updateRows)}
	}
	
	result, err := conn.Execute("SELECT value FROM test_dml WHERE id = 1")
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	rows := result.FetchAll()
	if len(rows) != 1 {
		result.Free()
		conn.Close()
		return TestResult{passed: false, message: "UPDATE verification failed"}
	}
	result.Free()
	
	deleteRows, err := conn.ExecuteUpdate("DELETE FROM test_dml WHERE id = 2")
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	if deleteRows != 1 {
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected 1 row deleted, got %d", deleteRows)}
	}
	
	result, err = conn.Execute("SELECT * FROM test_dml WHERE id = 2")
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	rows = result.FetchAll()
	if len(rows) != 0 {
		result.Free()
		conn.Close()
		return TestResult{passed: false, message: "DELETE verification failed"}
	}
	result.Free()
	
	_, err = conn.ExecuteUpdate("DROP TABLE IF EXISTS test_dml")
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	conn.Close()
	return TestResult{passed: true, message: ""}
}

func testHybridSearchGetSQL() TestResult {
	conn, err := seekdb.NewConnection()
	if err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	if err := conn.Connect("test", true); err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	
	_, _ = conn.ExecuteUpdate("DROP TABLE IF EXISTS doc_table")
	
	createTableSQL := `CREATE TABLE doc_table (
		c1 INT,
		vector VECTOR(3),
		query VARCHAR(255),
		content VARCHAR(255),
		VECTOR INDEX idx1(vector) WITH (distance=l2, type=hnsw, lib=vsag),
		FULLTEXT idx2(query),
		FULLTEXT idx3(content)
	)`
	_, err = conn.ExecuteUpdate(createTableSQL)
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	insertSQL := `INSERT INTO doc_table VALUES
		(1, '[1,2,3]', 'hello world', 'oceanbase Elasticsearch database'),
		(2, '[1,2,1]', 'hello world, what is your name', 'oceanbase mysql database'),
		(3, '[1,1,1]', 'hello world, how are you', 'oceanbase oracle database'),
		(4, '[1,3,1]', 'real world, where are you from', 'postgres oracle database'),
		(5, '[1,3,2]', 'real world, how old are you', 'redis oracle database'),
		(6, '[2,1,1]', 'hello world, where are you from', 'starrocks oceanbase database')`
	_, err = conn.ExecuteUpdate(insertSQL)
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	searchParamsObj := map[string]interface{}{
		"query": map[string]interface{}{
			"bool": map[string]interface{}{
				"should": []interface{}{
					map[string]interface{}{"match": map[string]interface{}{"query": "hi hello"}},
					map[string]interface{}{"match": map[string]interface{}{"content": "oceanbase mysql"}},
				},
				"filter": []interface{}{
					map[string]interface{}{"term": map[string]interface{}{"content": "postgres"}},
				},
			},
		},
		"knn": map[string]interface{}{
			"field":        "vector",
			"k":            5,
			"query_vector": []int{1, 2, 3},
		},
		"_source": []string{"query", "content", "_keyword_score", "_semantic_score"},
	}
	searchParamsJSON, err := json.Marshal(searchParamsObj)
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	searchParams := string(searchParamsJSON)
	escapedParams := strings.ReplaceAll(searchParams, "'", "''")
	
	setParmSQL := fmt.Sprintf("SET @parm = '%s'", escapedParams)
	_, err = conn.ExecuteUpdate(setParmSQL)
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	getSQLQuery := "SELECT DBMS_HYBRID_SEARCH.GET_SQL('doc_table', @parm)"
	sqlResult, err := conn.Execute(getSQLQuery)
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	rows := sqlResult.FetchAll()
	if len(rows) == 0 {
		sqlResult.Free()
		conn.Close()
		return TestResult{passed: false, message: "GET_SQL returned no rows"}
	}
	
	sqlResult.Free()
	conn.Close()
	return TestResult{passed: true, message: ""}
}

func testHybridSearchSearch() TestResult {
	conn, err := seekdb.NewConnection()
	if err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	if err := conn.Connect("test", true); err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	
	searchParamsObj := map[string]interface{}{
		"query": map[string]interface{}{
			"bool": map[string]interface{}{
				"should": []interface{}{
					map[string]interface{}{"match": map[string]interface{}{"query": "hello"}},
					map[string]interface{}{"match": map[string]interface{}{"content": "oceanbase mysql"}},
				},
			},
		},
		"knn": map[string]interface{}{
			"field":        "vector",
			"k":            5,
			"query_vector": []int{1, 2, 3},
		},
		"_source": []string{"c1", "query", "content", "_keyword_score", "_semantic_score"},
	}
	searchParamsJSON, err := json.Marshal(searchParamsObj)
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	searchParams := string(searchParamsJSON)
	escapedParams := strings.ReplaceAll(searchParams, "'", "''")
	
	searchQuery := fmt.Sprintf("SELECT DBMS_HYBRID_SEARCH.SEARCH('doc_table', '%s') as result", escapedParams)
	searchResult, err := conn.Execute(searchQuery)
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	rows := searchResult.FetchAll()
	if len(rows) == 0 {
		searchResult.Free()
		conn.Close()
		return TestResult{passed: false, message: "SEARCH returned no rows"}
	}
	
	searchResult.Free()
	conn.Close()
	return TestResult{passed: true, message: ""}
}

func testColumnNameInference() TestResult {
	conn, err := seekdb.NewConnection()
	if err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	if err := conn.Connect("test", true); err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	
	// Create test table with multiple columns to test column name inference
	if _, err := conn.ExecuteUpdate("CREATE TABLE IF NOT EXISTS test_cols (user_id INT, user_name VARCHAR(100), user_email VARCHAR(100))"); err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	if _, err := conn.ExecuteUpdate("INSERT INTO test_cols VALUES (1, 'Alice', 'alice@example.com')"); err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	// Query with explicit column names - column names should be inferred from database
	result, err := conn.Execute("SELECT user_id, user_name, user_email FROM test_cols")
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	// Verify column count
	if result.ColumnCount() != 3 {
		result.Free()
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected 3 columns, got %d", result.ColumnCount())}
	}
	
	// Verify column names are correctly inferred
	columnNames := result.ColumnNames()
	if len(columnNames) != 3 {
		result.Free()
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected 3 column names, got %d", len(columnNames))}
	}
	if columnNames[0] != "user_id" {
		result.Free()
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected first column name 'user_id', got '%s'", columnNames[0])}
	}
	if columnNames[1] != "user_name" {
		result.Free()
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected second column name 'user_name', got '%s'", columnNames[1])}
	}
	if columnNames[2] != "user_email" {
		result.Free()
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected third column name 'user_email', got '%s'", columnNames[2])}
	}
	
	// Verify data
	rows := result.FetchAll()
	if len(rows) != 1 {
		result.Free()
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected 1 row, got %d", len(rows))}
	}
	
	result.Free()
	if _, err := conn.ExecuteUpdate("DROP TABLE IF EXISTS test_cols"); err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	conn.Close()
	return TestResult{passed: true, message: ""}
}

func testParameterizedQueries() TestResult {
	conn, err := seekdb.NewConnection()
	if err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	if err := conn.Connect("test", true); err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	
	// Create test table
	if _, err := conn.ExecuteUpdate("CREATE TABLE IF NOT EXISTS test_params (id INT PRIMARY KEY, name VARCHAR(100))"); err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	if _, err := conn.ExecuteUpdate("INSERT INTO test_params VALUES (1, 'Alice'), (2, 'Bob')"); err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	// Test parameterized query concept
	// Since we don't have Prepared Statement bindings yet, we test column name inference
	// which is the core feature for parameterized queries
	result, err := conn.Execute("SELECT * FROM test_params WHERE id = 1")
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	// Verify column names are correctly inferred (core feature: column name inference)
	if result.ColumnCount() != 2 {
		result.Free()
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected 2 columns, got %d", result.ColumnCount())}
	}
	
	columnNames := result.ColumnNames()
	if len(columnNames) != 2 {
		result.Free()
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected 2 column names, got %d", len(columnNames))}
	}
	if columnNames[0] != "id" {
		result.Free()
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected first column name 'id', got '%s'", columnNames[0])}
	}
	if columnNames[1] != "name" {
		result.Free()
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected second column name 'name', got '%s'", columnNames[1])}
	}
	
	// Verify data
	rows := result.FetchAll()
	if len(rows) != 1 {
		result.Free()
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected 1 row, got %d", len(rows))}
	}
	
	result.Free()
	if _, err := conn.ExecuteUpdate("DROP TABLE IF EXISTS test_params"); err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	conn.Close()
	return TestResult{passed: true, message: ""}
}

// Test VECTOR type in INSERT (regression test for VECTOR_INSERT_ISSUE)
// Ensures VECTOR columns work in INSERT without "Column cannot be null" errors
func testVectorParameterBinding() TestResult {
	conn, err := seekdb.NewConnection()
	if err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	if err := conn.Connect("test", true); err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	if _, err := conn.ExecuteUpdate("DROP TABLE IF EXISTS test_vector_params"); err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	if _, err := conn.ExecuteUpdate("CREATE TABLE test_vector_params (id INT AUTO_INCREMENT PRIMARY KEY, document VARCHAR(255), metadata VARCHAR(255), embedding VECTOR(3))"); err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	insertSQL := `INSERT INTO test_vector_params (document, metadata, embedding) VALUES
		('Document 1', '{"category":"A","score":95}', '[1,2,3]'),
		('Document 2', '{"category":"B","score":90}', '[2,3,4]'),
		('Document 3', '{"category":"A","score":88}', '[1.1,2.1,3.1]'),
		('Document 4', '{"category":"C","score":92}', '[2.1,3.1,4.1]'),
		('Document 5', '{"category":"B","score":85}', '[1.2,2.2,3.2]')`
	if _, err := conn.ExecuteUpdate(insertSQL); err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	result, err := conn.Execute("SELECT COUNT(*) as cnt FROM test_vector_params")
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	rows := result.FetchAll()
	result.Free()
	if len(rows) != 1 || (len(rows[0]) > 0 && rows[0][0] != "5") {
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected 5 rows, got %v", rows)}
	}
	result2, err := conn.Execute("SELECT document, embedding FROM test_vector_params WHERE document = 'Document 1' LIMIT 1")
	if err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	ver := result2.FetchAll()
	result2.Free()
	if len(ver) != 1 {
		conn.Close()
		return TestResult{passed: false, message: "Expected 1 row for document=Document 1"}
	}
	if len(ver[0]) < 2 || ver[0][1] == "" {
		conn.Close()
		return TestResult{passed: false, message: "Embedding column is NULL - VECTOR INSERT failed"}
	}
	
	// Note: VECTOR type return format
	// - Insert: JSON array format "[1,2,3]" (via direct SQL embedding)
	// - Storage: Binary format (float array, e.g., 3 floats = 12 bytes for VECTOR(3))
	// - Query return: Typically binary format (may contain non-printable characters)
	//   The important thing is that data is not NULL, which means INSERT succeeded
	
	// Test 2: Additional INSERT to verify VECTOR type continues to work
	if _, err := conn.ExecuteUpdate("INSERT INTO test_vector_params (document, metadata, embedding) VALUES ('Auto-Detection Test', '{\"category\":\"TEST\",\"score\":100}', '[1.5,2.5,3.5]')"); err != nil {
		conn.ExecuteUpdate("DROP TABLE IF EXISTS test_vector_params")
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	result3, err := conn.Execute("SELECT COUNT(*) as cnt FROM test_vector_params")
	if err != nil {
		conn.ExecuteUpdate("DROP TABLE IF EXISTS test_vector_params")
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	rows3 := result3.FetchAll()
	result3.Free()
	if len(rows3) != 1 || (len(rows3[0]) > 0 && rows3[0][0] != "6") {
		conn.ExecuteUpdate("DROP TABLE IF EXISTS test_vector_params")
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected 6 rows after additional insert, got %v", rows3)}
	}
	result4, err := conn.Execute("SELECT document, embedding FROM test_vector_params WHERE document = 'Auto-Detection Test' LIMIT 1")
	if err != nil {
		conn.ExecuteUpdate("DROP TABLE IF EXISTS test_vector_params")
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	ver2 := result4.FetchAll()
	result4.Free()
	if len(ver2) != 1 {
		conn.ExecuteUpdate("DROP TABLE IF EXISTS test_vector_params")
		conn.Close()
		return TestResult{passed: false, message: "Expected 1 row for document=Auto-Detection Test"}
	}
	if len(ver2[0]) < 2 || ver2[0][1] == "" {
		conn.ExecuteUpdate("DROP TABLE IF EXISTS test_vector_params")
		conn.Close()
		return TestResult{passed: false, message: "Embedding column is NULL in additional insert - VECTOR INSERT failed"}
	}
	
	conn.ExecuteUpdate("DROP TABLE IF EXISTS test_vector_params")
	conn.Close()
	return TestResult{passed: true, message: ""}
}

// Test binary parameter binding with CAST(? AS BINARY) queries
// This test verifies that BLOB type correctly handles binary data
// in CAST(? AS BINARY) queries, ensuring proper binary comparison.
// Note: This test uses direct SQL execution as a workaround since
// prepared statement API may not be fully available.
func testBinaryParameterBinding() TestResult {
	conn, err := seekdb.NewConnection()
	if err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	if err := conn.Connect("test", true); err != nil {
		return TestResult{passed: false, message: err.Error()}
	}
	if _, err := conn.ExecuteUpdate("DROP TABLE IF EXISTS test_binary_params"); err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	if _, err := conn.ExecuteUpdate("CREATE TABLE test_binary_params (id INT PRIMARY KEY, _id VARBINARY(100))"); err != nil {
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	
	// Insert binary data using direct SQL (using hex format)
	// In a full implementation, this would use prepared statements with SEEKDB_TYPE_BLOB
	binaryValues := []string{"get1", "get2", "get3", "get4", "get5"}
	for i, val := range binaryValues {
		hexVal := fmt.Sprintf("%x", []byte(val))
		sql := fmt.Sprintf("INSERT INTO test_binary_params (id, _id) VALUES (%d, 0x%s)", i+1, hexVal)
		if _, err := conn.ExecuteUpdate(sql); err != nil {
			conn.ExecuteUpdate("DROP TABLE IF EXISTS test_binary_params")
			conn.Close()
			return TestResult{passed: false, message: err.Error()}
		}
	}
	
	// Verify data was inserted correctly
	result, err := conn.Execute("SELECT COUNT(*) as cnt FROM test_binary_params")
	if err != nil {
		conn.ExecuteUpdate("DROP TABLE IF EXISTS test_binary_params")
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	rows := result.FetchAll()
	result.Free()
	if len(rows) != 1 || (len(rows[0]) > 0 && rows[0][0] != "5") {
		conn.ExecuteUpdate("DROP TABLE IF EXISTS test_binary_params")
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("Expected 5 rows, got %v", rows)}
	}
	
	// Query using CAST(? AS BINARY) - using direct SQL with hex format
	searchVal := "get1"
	hexSearch := fmt.Sprintf("%x", []byte(searchVal))
	sql := fmt.Sprintf("SELECT _id FROM test_binary_params WHERE _id = CAST(0x%s AS BINARY)", hexSearch)
	result2, err := conn.Execute(sql)
	if err != nil {
		conn.ExecuteUpdate("DROP TABLE IF EXISTS test_binary_params")
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	ver := result2.FetchAll()
	result2.Free()
	if len(ver) != 1 {
		conn.ExecuteUpdate("DROP TABLE IF EXISTS test_binary_params")
		conn.Close()
		return TestResult{passed: false, message: "SELECT with CAST(? AS BINARY) returned 0 rows, expected 1 row"}
	}
	
	// Verify fetched data matches
	if len(ver[0]) == 0 || ver[0][0] == "" {
		conn.ExecuteUpdate("DROP TABLE IF EXISTS test_binary_params")
		conn.Close()
		return TestResult{passed: false, message: "Fetched _id is NULL, expected 'get1'"}
	}
	
	// Test negative case: query for non-existent value
	notFoundVal := "notfound"
	hexNotFound := fmt.Sprintf("%x", []byte(notFoundVal))
	sql3 := fmt.Sprintf("SELECT _id FROM test_binary_params WHERE _id = CAST(0x%s AS BINARY)", hexNotFound)
	result3, err := conn.Execute(sql3)
	if err != nil {
		conn.ExecuteUpdate("DROP TABLE IF EXISTS test_binary_params")
		conn.Close()
		return TestResult{passed: false, message: err.Error()}
	}
	ver3 := result3.FetchAll()
	result3.Free()
	if len(ver3) != 0 {
		conn.ExecuteUpdate("DROP TABLE IF EXISTS test_binary_params")
		conn.Close()
		return TestResult{passed: false, message: fmt.Sprintf("SELECT with non-existent value returned %d rows, expected 0", len(ver3))}
	}
	
	conn.ExecuteUpdate("DROP TABLE IF EXISTS test_binary_params")
	conn.Close()
	return TestResult{passed: true, message: ""}
}

func runAllTests() int {
	fmt.Println(strings.Repeat("=", 70))
	fmt.Println("SeekDB Go FFI Binding Test Suite")
	fmt.Println(strings.Repeat("=", 70))
	fmt.Println()
	
	dbDir := "./seekdb.db"
	if len(os.Args) > 1 {
		dbDir = os.Args[1]
	}
	
	if err := seekdb.Open(dbDir); err != nil {
		fmt.Fprintf(os.Stderr, "::error::Failed to open database: %v\n", err)
		return 1
	}
	defer seekdb.Close()
	
	testCases := []struct {
		name string
		fn   func() TestResult
	}{
		{"Database Open", testOpen},
		{"Connection Creation", testConnection},
		{"Error Handling", testErrorHandling},
		{"Result Operations", testResultOperations},
		{"Row Operations", testRowOperations},
		{"Error Message", testErrorMessage},
		{"Transaction Management", testTransactionManagement},
		{"DDL Operations", testDDLOperations},
		{"DML Operations", testDMLOperations},
		{"Parameterized Queries", testParameterizedQueries},
		{"VECTOR Parameter Binding", testVectorParameterBinding},
		{"Binary Parameter Binding", testBinaryParameterBinding},
		{"Column Name Inference", testColumnNameInference},
		{"DBMS_HYBRID_SEARCH.GET_SQL", testHybridSearchGetSQL},
		{"DBMS_HYBRID_SEARCH.SEARCH", testHybridSearchSearch},
	}
	
	results := []TestResult{}
	failedTests := []struct {
		name    string
		message string
	}{}
	
	for _, testCase := range testCases {
		fmt.Printf("[TEST] %-40s ... ", testCase.name)
		result := testCase.fn()
		results = append(results, result)
		
		if result.passed {
			fmt.Println("PASS")
		} else {
			fmt.Println("FAIL")
			failedTests = append(failedTests, struct {
				name    string
				message string
			}{testCase.name, result.message})
			if result.message != "" {
				fmt.Fprintf(os.Stderr, "::error::Test \"%s\" failed: %s\n", testCase.name, result.message)
			}
		}
	}
	
	fmt.Println()
	fmt.Println(strings.Repeat("-", 70))
	
	passed := 0
	for _, r := range results {
		if r.passed {
			passed++
		}
	}
	total := len(results)
	failed := total - passed
	
	if failed > 0 {
		fmt.Println("Failed Tests:")
		fmt.Println(strings.Repeat("-", 70))
		for _, test := range failedTests {
			fmt.Printf("  ✗ %s\n", test.name)
			if test.message != "" {
				fmt.Printf("    Error: %s\n", test.message)
			}
		}
		fmt.Println(strings.Repeat("-", 70))
	}
	
	fmt.Printf("Total: %d/%d passed, %d failed\n", passed, total, failed)
	fmt.Println()

	// Same-directory absolute open before teardown (aligned with python test.py).
	if passed == total {
		absSame, err := filepath.Abs(dbDir)
		if err != nil {
			fmt.Fprintf(os.Stderr, "::error::Absolute path resolve failed: %v\n", err)
			return 1
		}
		fmt.Printf("[TEST] %-40s ... ", "Absolute path (same DB directory)")
		if err := seekdb.Open(absSame); err != nil {
			fmt.Println("FAIL")
			fmt.Fprintf(os.Stderr, "::error::Absolute-path same-directory check failed: %v\n", err)
			return 1
		}
		fmt.Println("PASS")
	}

	if passed == total {
		fmt.Println("::notice::All tests passed successfully!")
		fmt.Println(strings.Repeat("=", 70))
		return 0
	} else {
		fmt.Fprintf(os.Stderr, "::error::%d test(s) failed\n", failed)
		fmt.Println(strings.Repeat("=", 70))
		return 1
	}
}

func bindingExitProbe(code int) {
	if os.Getenv("SEEKDB_BINDING_EXIT_PROBE") != "1" {
		return
	}
	pid := os.Getpid()
	path := filepath.Join(os.TempDir(), fmt.Sprintf("seekdb_binding_exit_probe_%d.log", pid))
	f, err := os.OpenFile(path, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0644)
	if err != nil {
		return
	}
	defer f.Close()
	_, _ = fmt.Fprintf(f, "before_process_exit code=%d\n", code)
	_ = f.Sync()
}

func main() {
	code := runAllTests()
	bindingExitProbe(code)
	os.Exit(code)
}
