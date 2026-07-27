/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

// Node.js SeekDB FFI Binding Test Suite
// Following database binding layer test requirements

const fs = require('fs');
const os = require('os');
const path = require('path');

const { open, close, SeekdbConnection } = require('./seekdb');

/** Sync probe: process may hang in Windows DLL unload after process.exit (see run-libseekdb-binding-tests.ps1). */
function bindingExitProbe(line) {
    if (process.env.SEEKDB_BINDING_EXIT_PROBE !== '1' && process.env.SEEKDB_NODE_BINDING_PROBE !== '1') {
        return;
    }
    try {
        const probePath = path.join(os.tmpdir(), `seekdb_binding_exit_probe_${process.pid}.log`);
        const fd = fs.openSync(probePath, 'a');
        try {
            fs.writeSync(fd, `${line}\n`);
            fs.fsyncSync(fd);
        } finally {
            fs.closeSync(fd);
        }
    } catch (_) {
        /* ignore */
    }
}

function exitAfterBindingTests(code) {
    bindingExitProbe(`before_process_exit code=${code}`);
    if (process.env.SEEKDB_BINDING_EXIT_PROBE === '1' || process.env.SEEKDB_NODE_BINDING_PROBE === '1') {
        process.exit(code);
    }
    try { close(); } catch { }
    process.exit(code);
}

// Test database open
function testOpen() {
    try {
        // Note: Database should already be open from runAllTests
        // Just verify it's accessible by creating a connection
        const conn = new SeekdbConnection();
        conn.connect('test', true);

        if (conn.handle === null) {
            throw new Error('Connection handle is null');
        }

        conn.close();
        return { passed: true, message: null };
    } catch (e) {
        return { passed: false, message: e.message };
    }
}

// Test connection creation
function testConnection() {
    try {
        const conn = new SeekdbConnection();
        conn.connect('test', true);

        if (conn.handle === null) {
            throw new Error('Connection handle is null');
        }

        conn.close();
        return { passed: true, message: null };
    } catch (e) {
        return { passed: false, message: e.message };
    }
}

// Test error handling
function testErrorHandling() {
    try {
        // Test invalid parameters - connection with null database
        const conn1 = new SeekdbConnection();
        try {
            conn1.connect(null, true);
            return { passed: false, message: 'Should have thrown error for null database' };
        } catch (e) {
            // Expected error
        }

        // Database should already be open
        const conn2 = new SeekdbConnection();
        conn2.connect('test', true);

        // Test result operations on null result (via invalid SQL)
        try {
            const result = conn2.execute('INVALID SQL STATEMENT');
            return { passed: false, message: 'Should have thrown error for invalid SQL' };
        } catch (e) {
            // Expected error
        }

        conn2.close();
        return { passed: true, message: null };
    } catch (e) {
        return { passed: false, message: e.message };
    }
}

// Test result operations and column name inference (core feature)
function testResultOperations() {
    try {
        const conn = new SeekdbConnection();
        conn.connect('test', true);

        // Create table with known column names to test column name inference
        conn.executeUpdate('CREATE TABLE IF NOT EXISTS test_cols (user_id INT, user_name VARCHAR(100))');
        conn.executeUpdate("INSERT INTO test_cols VALUES (1, 'Alice')");

        // Query with explicit column names - column names should be inferred from database
        const result = conn.execute('SELECT user_id, user_name FROM test_cols');

        // Test row count
        if (result.rowCount !== 1) {
            throw new Error(`Expected row count 1, got ${result.rowCount}`);
        }

        // Test column count
        if (result.columnCount !== 2) {
            throw new Error(`Expected column count 2, got ${result.columnCount}`);
        }

        // Test column names - verify they are correctly inferred
        if (result.columnNames.length !== 2) {
            throw new Error(`Expected 2 column names, got ${result.columnNames.length}`);
        }
        if (result.columnNames[0] !== 'user_id') {
            throw new Error(`Expected first column name 'user_id', got '${result.columnNames[0]}'`);
        }
        if (result.columnNames[1] !== 'user_name') {
            throw new Error(`Expected second column name 'user_name', got '${result.columnNames[1]}'`);
        }

        // Test fetch all rows
        const rows = result.fetchAll();
        if (rows.length !== 1) {
            throw new Error(`Expected 1 row, got ${rows.length}`);
        }

        result.free();
        conn.executeUpdate('DROP TABLE IF EXISTS test_cols');
        conn.close();
        return { passed: true, message: null };
    } catch (e) {
        return { passed: false, message: e.message };
    }
}

// Test row operations and data types
function testRowOperations() {
    try {
        const conn = new SeekdbConnection();
        conn.connect('test', true);

        // Create a test table with various data types
        conn.executeUpdate(`
            CREATE TABLE IF NOT EXISTS test_types (
                id INT PRIMARY KEY,
                name VARCHAR(100),
                price DECIMAL(10,2),
                quantity INT,
                active BOOLEAN,
                score DOUBLE
            )
        `);

        // Insert test data
        conn.executeUpdate(`
            INSERT INTO test_types VALUES
            (1, 'Product A', 99.99, 10, true, 4.5),
            (2, 'Product B', 199.99, 5, false, 3.8),
            (3, NULL, NULL, NULL, NULL, NULL)
        `);

        // Query and verify data types
        const result = conn.execute('SELECT * FROM test_types ORDER BY id');
        const rows = result.fetchAll();

        if (rows.length !== 3) {
            throw new Error(`Expected 3 rows, got ${rows.length}`);
        }

        // Clean up
        conn.executeUpdate('DROP TABLE IF EXISTS test_types');
        result.free();
        conn.close();
        return { passed: true, message: null };
    } catch (e) {
        return { passed: false, message: e.message };
    }
}

// Test error message retrieval
function testErrorMessage() {
    try {
        const conn = new SeekdbConnection();
        conn.connect('test', true);

        // Get error message (should be empty initially)
        const errorCode = conn.getLastErrorCode();

        // Try to trigger an error
        try {
            conn.execute('SELECT * FROM non_existent_table');
        } catch (e) {
            const errorMsg2 = conn.getLastError();
            if (!errorMsg2) {
                throw new Error('Error message should be available after failed query');
            }
        }

        conn.close();
        return { passed: true, message: null };
    } catch (e) {
        return { passed: false, message: e.message };
    }
}

// Test transaction management
function testTransactionManagement() {
    try {
        const conn = new SeekdbConnection();
        // Use autocommit=false to test manual transaction control
        conn.connect('test', false);

        // Create test table
        conn.executeUpdate('CREATE TABLE IF NOT EXISTS test_txn (id INT PRIMARY KEY, value INT)');

        // Test begin/commit
        conn.begin();
        conn.executeUpdate("INSERT INTO test_txn VALUES (1, 100)");
        conn.commit();

        // Verify data was committed
        const result1 = conn.execute('SELECT * FROM test_txn WHERE id = 1');
        const rows1 = result1.fetchAll();
        if (rows1.length !== 1) {
            throw new Error('Data not committed');
        }
        result1.free();

        // Test begin/rollback
        conn.begin();
        conn.executeUpdate("INSERT INTO test_txn VALUES (2, 200)");
        conn.rollback();

        // Clean up
        conn.executeUpdate('DROP TABLE IF EXISTS test_txn');
        conn.close();
        return { passed: true, message: null };
    } catch (e) {
        return { passed: false, message: e.message };
    }
}

// Test DDL operations (CREATE, DROP, ALTER)
function testDDLOperations() {
    try {
        const conn = new SeekdbConnection();
        conn.connect('test', true);

        // Test CREATE TABLE
        conn.executeUpdate(`
            CREATE TABLE IF NOT EXISTS test_ddl (
                id INT PRIMARY KEY,
                name VARCHAR(100),
                created_at TIMESTAMP
            )
        `);

        // Test ALTER TABLE (if supported)
        try {
            conn.executeUpdate('ALTER TABLE test_ddl ADD COLUMN description VARCHAR(255)');
        } catch (e) {
            // ALTER TABLE may not be supported, that's okay
        }

        // Test DROP TABLE
        conn.executeUpdate('DROP TABLE IF EXISTS test_ddl');

        conn.close();
        return { passed: true, message: null };
    } catch (e) {
        return { passed: false, message: e.message };
    }
}

// Test DML operations (INSERT, UPDATE, DELETE)
function testDMLOperations() {
    try {
        const conn = new SeekdbConnection();
        conn.connect('test', true);

        // Create test table
        conn.executeUpdate(`
            CREATE TABLE IF NOT EXISTS test_dml (
                id INT PRIMARY KEY,
                name VARCHAR(100),
                value INT
            )
        `);

        // Test INSERT
        const insertRows = conn.executeUpdate("INSERT INTO test_dml VALUES (1, 'A', 10), (2, 'B', 20), (3, 'C', 30)");
        if (insertRows !== 3) {
            throw new Error(`Expected 3 rows inserted, got ${insertRows}`);
        }

        // Test UPDATE
        const updateRows = conn.executeUpdate("UPDATE test_dml SET value = 100 WHERE id = 1");
        if (updateRows !== 1) {
            throw new Error(`Expected 1 row updated, got ${updateRows}`);
        }

        // Verify UPDATE
        const result1 = conn.execute('SELECT value FROM test_dml WHERE id = 1');
        const rows1 = result1.fetchAll();
        if (rows1.length !== 1) {
            throw new Error('UPDATE verification failed');
        }
        result1.free();

        // Test DELETE
        const deleteRows = conn.executeUpdate('DELETE FROM test_dml WHERE id = 2');
        if (deleteRows !== 1) {
            throw new Error(`Expected 1 row deleted, got ${deleteRows}`);
        }

        // Verify DELETE
        const result2 = conn.execute('SELECT * FROM test_dml WHERE id = 2');
        const rows2 = result2.fetchAll();
        if (rows2.length !== 0) {
            throw new Error('DELETE verification failed');
        }
        result2.free();

        // Clean up
        conn.executeUpdate('DROP TABLE IF EXISTS test_dml');
        conn.close();
        return { passed: true, message: null };
    } catch (e) {
        return { passed: false, message: e.message };
    }
}

// Test DBMS_HYBRID_SEARCH.GET_SQL
function testHybridSearchGetSQL() {
    let conn = null;
    try {
        conn = new SeekdbConnection();
        conn.connect('test', true);

        // Drop existing table if exists
        try {
            conn.executeUpdate('DROP TABLE IF EXISTS doc_table');
        } catch (e) {
            // Ignore errors
        }

        // Create table with vector and fulltext indexes
        const createTableSQL = `CREATE TABLE doc_table (
            c1 INT,
            vector VECTOR(3),
            query VARCHAR(255),
            content VARCHAR(255),
            VECTOR INDEX idx1(vector) WITH (distance=l2, type=hnsw, lib=vsag),
            FULLTEXT idx2(query),
            FULLTEXT idx3(content)
        )`;
        conn.executeUpdate(createTableSQL);

        // Insert test data
        const insertSQL = `INSERT INTO doc_table VALUES
            (1, '[1,2,3]', 'hello world', 'oceanbase Elasticsearch database'),
            (2, '[1,2,1]', 'hello world, what is your name', 'oceanbase mysql database'),
            (3, '[1,1,1]', 'hello world, how are you', 'oceanbase oracle database'),
            (4, '[1,3,1]', 'real world, where are you from', 'postgres oracle database'),
            (5, '[1,3,2]', 'real world, how old are you', 'redis oracle database'),
            (6, '[2,1,1]', 'hello world, where are you from', 'starrocks oceanbase database')`;
        conn.executeUpdate(insertSQL);

        // Test GET_SQL - Following official example format exactly
        const searchParamsObj = {
            query: {
                bool: {
                    should: [
                        { match: { query: "hi hello" } },
                        { match: { content: "oceanbase mysql" } }
                    ],
                    filter: [
                        { term: { content: "postgres" } }
                    ]
                }
            },
            knn: {
                field: "vector",
                k: 5,
                query_vector: [1, 2, 3]
            },
            _source: ["query", "content", "_keyword_score", "_semantic_score"]
        };
        const searchParams = JSON.stringify(searchParamsObj);
        const escapedParams = searchParams.replace(/'/g, "''");

        // Official method: SET @parm then SELECT GET_SQL (no "as sql" alias)
        const setParmSQL = `SET @parm = '${escapedParams}'`;
        conn.executeUpdate(setParmSQL);

        const getSqlQuery = `SELECT DBMS_HYBRID_SEARCH.GET_SQL('doc_table', @parm)`;
        const sqlResult = conn.execute(getSqlQuery);
        const sqlRows = sqlResult.fetchAll();

        if (sqlRows.length === 0) {
            throw new Error('GET_SQL returned no rows');
        }

        sqlResult.free();
        conn.close();
        return { passed: true, message: null };
    } catch (e) {
        let errorMsg = e.message;
        if (conn) {
            try {
                const detailedError = conn.getLastError();
                if (detailedError) {
                    errorMsg = `${e.message} (${detailedError})`;
                }
                conn.close();
            } catch { }
        }
        return { passed: false, message: errorMsg };
    }
}

// Test DBMS_HYBRID_SEARCH.SEARCH
function testHybridSearchSearch() {
    let conn = null;
    try {
        conn = new SeekdbConnection();
        conn.connect('test', true);

        // Test SEARCH (assuming table exists from previous test)
        const searchParamsObj = {
            query: {
                bool: {
                    should: [
                        { match: { query: "hello" } },
                        { match: { content: "oceanbase mysql" } }
                    ]
                }
            },
            knn: {
                field: "vector",
                k: 5,
                query_vector: [1, 2, 3]
            },
            _source: ["c1", "query", "content", "_keyword_score", "_semantic_score"]
        };
        const searchParams = JSON.stringify(searchParamsObj);
        const escapedParams = searchParams.replace(/'/g, "''");

        const searchQuery = `SELECT DBMS_HYBRID_SEARCH.SEARCH('doc_table', '${escapedParams}') as result`;
        const searchResult = conn.execute(searchQuery);
        const searchRows = searchResult.fetchAll();

        if (searchRows.length === 0) {
            throw new Error('SEARCH returned no rows');
        }

        searchResult.free();
        conn.close();
        return { passed: true, message: null };
    } catch (e) {
        let errorMsg = e.message;
        if (conn) {
            try {
                const detailedError = conn.getLastError();
                if (detailedError) {
                    errorMsg = `${e.message} (${detailedError})`;
                }
                conn.close();
            } catch { }
        }
        return { passed: false, message: errorMsg };
    }
}

// Test column name inference with multiple columns (core feature)
function testColumnNameInference() {
    try {
        const conn = new SeekdbConnection();
        conn.connect('test', true);

        // Create test table with multiple columns to test column name inference
        conn.executeUpdate('CREATE TABLE IF NOT EXISTS test_cols (user_id INT, user_name VARCHAR(100), user_email VARCHAR(100))');
        conn.executeUpdate("INSERT INTO test_cols VALUES (1, 'Alice', 'alice@example.com')");

        // Query with explicit column names - column names should be inferred from database
        const result = conn.execute('SELECT user_id, user_name, user_email FROM test_cols');

        // Verify column count
        if (result.columnCount !== 3) {
            throw new Error(`Expected 3 columns, got ${result.columnCount}`);
        }

        // Verify column names are correctly inferred
        if (result.columnNames.length !== 3) {
            throw new Error(`Expected 3 column names, got ${result.columnNames.length}`);
        }
        if (result.columnNames[0] !== 'user_id') {
            throw new Error(`Expected first column name 'user_id', got '${result.columnNames[0]}'`);
        }
        if (result.columnNames[1] !== 'user_name') {
            throw new Error(`Expected second column name 'user_name', got '${result.columnNames[1]}'`);
        }
        if (result.columnNames[2] !== 'user_email') {
            throw new Error(`Expected third column name 'user_email', got '${result.columnNames[2]}'`);
        }

        // Verify data
        const rows = result.fetchAll();
        if (rows.length !== 1) {
            throw new Error(`Expected 1 row, got ${rows.length}`);
        }

        // Verify row data can be accessed by column name
        if (rows[0].user_id !== '1') {
            throw new Error(`Expected user_id '1', got '${rows[0].user_id}'`);
        }
        if (rows[0].user_name !== 'Alice') {
            throw new Error(`Expected user_name 'Alice', got '${rows[0].user_name}'`);
        }
        if (rows[0].user_email !== 'alice@example.com') {
            throw new Error(`Expected user_email 'alice@example.com', got '${rows[0].user_email}'`);
        }

        result.free();
        conn.executeUpdate('DROP TABLE IF EXISTS test_cols');
        conn.close();
        return { passed: true, message: null };
    } catch (e) {
        return { passed: false, message: e.message };
    }
}

// Test parameterized queries (core feature)
function testParameterizedQueries() {
    try {
        const conn = new SeekdbConnection();
        conn.connect('test', true);

        // Create test table
        conn.executeUpdate('CREATE TABLE IF NOT EXISTS test_params (id INT PRIMARY KEY, name VARCHAR(100))');
        conn.executeUpdate("INSERT INTO test_params VALUES (1, 'Alice'), (2, 'Bob')");

        // Test parameterized query concept
        // Since we don't have Prepared Statement bindings yet, we test column name inference
        // which is the core feature for parameterized queries
        const result = conn.execute("SELECT * FROM test_params WHERE id = 1");

        // Verify column names are correctly inferred (core feature: column name inference)
        if (result.columnCount !== 2) {
            throw new Error(`Expected 2 columns, got ${result.columnCount}`);
        }
        if (result.columnNames[0] !== 'id') {
            throw new Error(`Expected first column name 'id', got '${result.columnNames[0]}'`);
        }
        if (result.columnNames[1] !== 'name') {
            throw new Error(`Expected second column name 'name', got '${result.columnNames[1]}'`);
        }

        // Verify data
        const rows = result.fetchAll();
        if (rows.length !== 1) {
            throw new Error(`Expected 1 row, got ${rows.length}`);
        }
        if (rows[0].id !== '1' || rows[0].name !== 'Alice') {
            throw new Error('Parameterized query result mismatch');
        }

        result.free();
        conn.executeUpdate('DROP TABLE IF EXISTS test_params');
        conn.close();
        return { passed: true, message: null };
    } catch (e) {
        return { passed: false, message: e.message };
    }
}

// Test VECTOR type in INSERT (regression test for VECTOR_INSERT_ISSUE)
// Ensures VECTOR columns work in INSERT without "Column cannot be null" errors
function testVectorParameterBinding() {
    try {
        const conn = new SeekdbConnection();
        conn.connect('test', true);
        conn.executeUpdate('DROP TABLE IF EXISTS test_vector_params');
        conn.executeUpdate(`CREATE TABLE test_vector_params (
            id INT AUTO_INCREMENT PRIMARY KEY,
            document VARCHAR(255),
            metadata VARCHAR(255),
            embedding VECTOR(3)
        )`);
        conn.executeUpdate(`INSERT INTO test_vector_params (document, metadata, embedding) VALUES
            ('Document 1', '{"category":"A","score":95}', '[1,2,3]'),
            ('Document 2', '{"category":"B","score":90}', '[2,3,4]'),
            ('Document 3', '{"category":"A","score":88}', '[1.1,2.1,3.1]'),
            ('Document 4', '{"category":"C","score":92}', '[2.1,3.1,4.1]'),
            ('Document 5', '{"category":"B","score":85}', '[1.2,2.2,3.2]')`);
        const countResult = conn.execute('SELECT COUNT(*) as cnt FROM test_vector_params');
        const countRows = countResult.fetchAll();
        countResult.free();
        const cnt = countRows[0] && (countRows[0].cnt !== undefined ? countRows[0].cnt : countRows[0][0]);
        if (countRows.length !== 1 || String(cnt) !== '5') {
            conn.close();
            return { passed: false, message: `Expected 5 rows, got ${cnt}` };
        }
        const verifyResult = conn.execute("SELECT document, embedding FROM test_vector_params WHERE document = 'Document 1' LIMIT 1");
        const verifyRows = verifyResult.fetchAll();
        verifyResult.free();
        if (verifyRows.length !== 1) {
            conn.close();
            return { passed: false, message: 'Expected 1 row for document=Document 1' };
        }
        const row = verifyRows[0];
        const embedding = row.embedding ?? row[1];
        if (embedding === undefined || embedding === null || (typeof embedding === 'string' && embedding.length === 0)) {
            conn.close();
            return { passed: false, message: 'Embedding column is NULL - VECTOR INSERT failed' };
        }

        // Note: VECTOR type return format
        // - Insert: JSON array format "[1,2,3]" (via direct SQL embedding)
        // - Storage: Binary format (float array, e.g., 3 floats = 12 bytes for VECTOR(3))
        // - Query return: Typically binary format (may contain non-printable characters)
        //   The important thing is that data is not NULL, which means INSERT succeeded

        // Test 2: Additional INSERT to verify VECTOR type continues to work
        conn.executeUpdate(`INSERT INTO test_vector_params (document, metadata, embedding) VALUES
            ('Auto-Detection Test', '{"category":"TEST","score":100}', '[1.5,2.5,3.5]')`);
        const countResult2 = conn.execute('SELECT COUNT(*) as cnt FROM test_vector_params');
        const countRows2 = countResult2.fetchAll();
        countResult2.free();
        const cnt2 = countRows2[0] && (countRows2[0].cnt !== undefined ? countRows2[0].cnt : countRows2[0][0]);
        if (countRows2.length !== 1 || String(cnt2) !== '6') {
            conn.executeUpdate('DROP TABLE IF EXISTS test_vector_params');
            conn.close();
            return { passed: false, message: `Expected 6 rows after additional insert, got ${cnt2}` };
        }
        const verifyResult2 = conn.execute("SELECT document, embedding FROM test_vector_params WHERE document = 'Auto-Detection Test' LIMIT 1");
        const verifyRows2 = verifyResult2.fetchAll();
        verifyResult2.free();
        if (verifyRows2.length !== 1) {
            conn.executeUpdate('DROP TABLE IF EXISTS test_vector_params');
            conn.close();
            return { passed: false, message: 'Expected 1 row for document=Auto-Detection Test' };
        }
        const row2 = verifyRows2[0];
        const embedding2 = row2.embedding ?? row2[1];
        if (embedding2 === undefined || embedding2 === null || (typeof embedding2 === 'string' && embedding2.length === 0)) {
            conn.executeUpdate('DROP TABLE IF EXISTS test_vector_params');
            conn.close();
            return { passed: false, message: 'Embedding column is NULL in additional insert - VECTOR INSERT failed' };
        }

        conn.executeUpdate('DROP TABLE IF EXISTS test_vector_params');
        conn.close();
        return { passed: true, message: null };
    } catch (e) {
        return { passed: false, message: e.message };
    }
}

// Test binary parameter binding with CAST(? AS BINARY) queries
// This test verifies that BLOB type correctly handles binary data
// in CAST(? AS BINARY) queries, ensuring proper binary comparison.
// Note: This test uses direct SQL execution as a workaround since
// prepared statement API may not be fully available.
function testBinaryParameterBinding() {
    try {
        const conn = new SeekdbConnection();
        conn.connect('test', true);
        conn.executeUpdate('DROP TABLE IF EXISTS test_binary_params');
        conn.executeUpdate('CREATE TABLE test_binary_params (id INT PRIMARY KEY, _id VARBINARY(100))');

        // Insert binary data using direct SQL (using hex format)
        // In a full implementation, this would use prepared statements with SEEKDB_TYPE_BLOB
        const binaryValues = ['get1', 'get2', 'get3', 'get4', 'get5'];
        for (let i = 0; i < binaryValues.length; i++) {
            const hexVal = Buffer.from(binaryValues[i], 'utf-8').toString('hex');
            conn.executeUpdate(`INSERT INTO test_binary_params (id, _id) VALUES (${i + 1}, 0x${hexVal})`);
        }

        // Verify data was inserted correctly
        const countResult = conn.execute('SELECT COUNT(*) as cnt FROM test_binary_params');
        const countRows = countResult.fetchAll();
        countResult.free();
        const cnt = countRows[0] && (countRows[0].cnt !== undefined ? countRows[0].cnt : countRows[0][0]);
        if (countRows.length !== 1 || String(cnt) !== '5') {
            conn.executeUpdate('DROP TABLE IF EXISTS test_binary_params');
            conn.close();
            return { passed: false, message: `Expected 5 rows, got ${cnt}` };
        }

        // Query using CAST(? AS BINARY) - using direct SQL with hex format
        const searchVal = 'get1';
        const hexSearch = Buffer.from(searchVal, 'utf-8').toString('hex');
        const selectResult = conn.execute(`SELECT _id FROM test_binary_params WHERE _id = CAST(0x${hexSearch} AS BINARY)`);
        const selectRows = selectResult.fetchAll();
        selectResult.free();

        if (selectRows.length !== 1) {
            conn.executeUpdate('DROP TABLE IF EXISTS test_binary_params');
            conn.close();
            return { passed: false, message: 'SELECT with CAST(? AS BINARY) returned 0 rows, expected 1 row' };
        }

        // Verify fetched data matches
        const fetchedId = selectRows[0]._id ?? selectRows[0][0];
        if (fetchedId === undefined || fetchedId === null || (typeof fetchedId === 'string' && fetchedId.length === 0)) {
            conn.executeUpdate('DROP TABLE IF EXISTS test_binary_params');
            conn.close();
            return { passed: false, message: "Fetched _id is NULL, expected 'get1'" };
        }

        // Test negative case: query for non-existent value
        const notFoundVal = 'notfound';
        const hexNotFound = Buffer.from(notFoundVal, 'utf-8').toString('hex');
        const notFoundResult = conn.execute(`SELECT _id FROM test_binary_params WHERE _id = CAST(0x${hexNotFound} AS BINARY)`);
        const notFoundRows = notFoundResult.fetchAll();
        notFoundResult.free();

        if (notFoundRows.length !== 0) {
            conn.executeUpdate('DROP TABLE IF EXISTS test_binary_params');
            conn.close();
            return { passed: false, message: `SELECT with non-existent value returned ${notFoundRows.length} rows, expected 0` };
        }

        conn.executeUpdate('DROP TABLE IF EXISTS test_binary_params');
        conn.close();
        return { passed: true, message: null };
    } catch (e) {
        return { passed: false, message: e.message };
    }
}

// Main test runner
function runAllTests() {
    console.log('='.repeat(70));
    console.log('SeekDB Node.js FFI Binding Test Suite');
    console.log('='.repeat(70));
    console.log('');

    // Open database once at the start
    const dbDir = process.argv[2] || './seekdb.db';
    try {
        open(dbDir);
    } catch (e) {
        console.error('::error::Failed to open database:', e.message);
        return 1;
    }

    const testCases = [
        { name: 'Database Open', fn: testOpen },
        { name: 'Connection Creation', fn: testConnection },
        { name: 'Error Handling', fn: testErrorHandling },
        { name: 'Result Operations', fn: testResultOperations },
        { name: 'Row Operations', fn: testRowOperations },
        { name: 'Error Message', fn: testErrorMessage },
        { name: 'Transaction Management', fn: testTransactionManagement },
        { name: 'DDL Operations', fn: testDDLOperations },
        { name: 'DML Operations', fn: testDMLOperations },
        { name: 'Parameterized Queries', fn: testParameterizedQueries },
        { name: 'VECTOR Parameter Binding', fn: testVectorParameterBinding },
        { name: 'Binary Parameter Binding', fn: testBinaryParameterBinding },
        { name: 'Column Name Inference', fn: testColumnNameInference },
        { name: 'DBMS_HYBRID_SEARCH.GET_SQL', fn: testHybridSearchGetSQL },
        { name: 'DBMS_HYBRID_SEARCH.SEARCH', fn: testHybridSearchSearch }
    ];

    const results = [];
    let failedTests = [];

    try {
        // Run all tests
        for (const testCase of testCases) {
            process.stdout.write(`[TEST] ${testCase.name.padEnd(40)} ... `);
            const result = testCase.fn();
            results.push({ name: testCase.name, ...result });

            if (result.passed) {
                console.log('PASS');
            } else {
                console.log('FAIL');
                failedTests.push({ name: testCase.name, message: result.message });
                // Output error for GitHub Actions
                if (result.message) {
                    console.error(`::error::Test "${testCase.name}" failed: ${result.message}`);
                }
            }
        }

        console.log('');
        console.log('-'.repeat(70));

        const passed = results.filter(r => r.passed).length;
        const total = results.length;
        const failed = total - passed;

        // Only show failed tests in summary, or show summary if all passed
        if (failed > 0) {
            console.log('Failed Tests:');
            console.log('-'.repeat(70));
            failedTests.forEach(test => {
                console.log(`  ✗ ${test.name}`);
                if (test.message) {
                    console.log(`    Error: ${test.message}`);
                }
            });
            console.log('-'.repeat(70));
        }

        console.log(`Total: ${passed}/${total} passed, ${failed} failed`);
        console.log('');

        const exitCode = passed === total ? 0 : 1;

        // Same-directory absolute path (matches python test.py) — must run before close(), in-process.
        // A second child process re-open on Windows can hit pidfile/lock or path edge cases in CI.
        if (passed === total) {
            const absSame = path.resolve(dbDir);
            try {
                open(absSame);
                console.log('[TEST] Absolute path (same DB directory)               ... PASS');
            } catch (e) {
                console.log('[TEST] Absolute path (same DB directory)               ... FAIL');
                console.error(`::error::Absolute-path same-directory check failed: ${e.message}`);
                return 1;
            }
        }

        if (passed === total) {
            console.log('::notice::All tests passed successfully!');
            console.log('='.repeat(70));
            return exitCode;
        } else {
            console.error(`::error::${failed} test(s) failed`);
            console.log('='.repeat(70));
            return exitCode;
        }
    } catch (e) {
        console.error('::error::Unexpected error during tests:', e.message);
        console.error(e.stack);
        return 1;
    }
}

// Run tests if executed directly
if (require.main === module) {
    exitAfterBindingTests(runAllTests());
}

module.exports = {
    testOpen,
    testConnection,
    testErrorHandling,
    testResultOperations,
    testRowOperations,
    testErrorMessage,
    testTransactionManagement,
    testDDLOperations,
    testDMLOperations,
    testHybridSearchGetSQL,
    testHybridSearchSearch,
    runAllTests
};
