/*
 * Copyright (c) 2025 OceanBase.
 * Test script for N-API SeekDB binding
 */

const path = require('path');
const fs = require('fs');
const os = require('os');
const util = require('util');

// When stdout is not a TTY (e.g. GitHub Actions / Start-Process inherit), Node may block-buffer console.log;
// CI then shows only the first chunk and "still waiting" heartbeats while tests run or hang in native code.
if (!process.stdout.isTTY) {
    const syncLine = (fd, args) => {
        try {
            fs.writeSync(fd, util.format(...args) + '\n');
        } catch (_) {
            /* fall back */
            if (fd === 1) {
                console.log(...args);
            } else {
                console.error(...args);
            }
        }
    };
    console.log = (...a) => syncLine(1, a);
    console.info = (...a) => syncLine(1, a);
    console.error = (...a) => syncLine(2, a);
    console.warn = (...a) => syncLine(2, a);
}

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
    try { seekdb.close(); } catch { }
    process.exit(code);
}

// Load the native module
const seekdb = require('./build/Release/seekdb.node');

// Test functions
function testOpen() {
    try {
        const conn = seekdb.connect('test', true);
        seekdb.connectClose(conn);
        return { passed: true, message: null };
    } catch (e) {
        return { passed: false, message: e.message };
    }
}

function testConnection() {
    try {
        const conn = seekdb.connect('test', true);
        if (!conn) {
            throw new Error('Connection handle is null');
        }
        seekdb.connectClose(conn);
        return { passed: true, message: null };
    } catch (e) {
        return { passed: false, message: e.message };
    }
}

function testErrorHandling() {
    try {
        // Test invalid SQL
        const conn = seekdb.connect('test', true);
        try {
            seekdb.query(conn, 'INVALID SQL STATEMENT');
            seekdb.connectClose(conn);
            return { passed: false, message: 'Should have thrown error for invalid SQL' };
        } catch (e) {
            // Expected error
        }
        seekdb.connectClose(conn);
        return { passed: true, message: null };
    } catch (e) {
        return { passed: false, message: e.message };
    }
}

function testResultOperations() {
    try {
        const conn = seekdb.connect('test', true);
        const result = seekdb.query(conn, 'SELECT 1 as id, "hello" as message, 3.14 as price, true as active');

        if (result.rowCount !== 1) {
            throw new Error(`Expected row count 1, got ${result.rowCount}`);
        }
        if (result.columnCount !== 4) {
            throw new Error(`Expected column count 4, got ${result.columnCount}`);
        }

        const rows = result.fetchAll();
        if (rows.length !== 1) {
            throw new Error(`Expected 1 row, got ${rows.length}`);
        }

        seekdb.connectClose(conn);
        return { passed: true, message: null };
    } catch (e) {
        return { passed: false, message: e.message };
    }
}

function testRowOperations() {
    try {
        const conn = seekdb.connect('test', true);

        seekdb.query(conn, `
            CREATE TABLE IF NOT EXISTS test_types (
                id INT PRIMARY KEY,
                name VARCHAR(100),
                price DECIMAL(10,2),
                quantity INT,
                active BOOLEAN,
                score DOUBLE
            )
        `);

        seekdb.query(conn, `
            INSERT INTO test_types VALUES
            (1, 'Product A', 99.99, 10, true, 4.5),
            (2, 'Product B', 199.99, 5, false, 3.8),
            (3, NULL, NULL, NULL, NULL, NULL)
        `);

        const result = seekdb.query(conn, 'SELECT * FROM test_types ORDER BY id');
        const rows = result.fetchAll();

        if (rows.length !== 3) {
            throw new Error(`Expected 3 rows, got ${rows.length}`);
        }

        seekdb.query(conn, 'DROP TABLE IF EXISTS test_types');
        seekdb.connectClose(conn);
        return { passed: true, message: null };
    } catch (e) {
        return { passed: false, message: e.message };
    }
}

function testErrorMessage() {
    try {
        const conn = seekdb.connect('test', true);

        try {
            seekdb.query(conn, 'SELECT * FROM non_existent_table');
        } catch (e) {
            // Expected error
        }

        seekdb.connectClose(conn);
        return { passed: true, message: null };
    } catch (e) {
        return { passed: false, message: e.message };
    }
}

function testTransactionManagement() {
    try {
        const conn = seekdb.connect('test', false);

        seekdb.query(conn, 'CREATE TABLE IF NOT EXISTS test_txn (id INT PRIMARY KEY, value INT)');

        seekdb.begin(conn);
        seekdb.query(conn, "INSERT INTO test_txn VALUES (1, 100)");
        seekdb.commit(conn);

        const result = seekdb.query(conn, 'SELECT * FROM test_txn WHERE id = 1');
        const rows = result.fetchAll();
        if (rows.length !== 1) {
            throw new Error('Data not committed');
        }

        seekdb.begin(conn);
        seekdb.query(conn, "INSERT INTO test_txn VALUES (2, 200)");
        seekdb.rollback(conn);

        seekdb.query(conn, 'DROP TABLE IF EXISTS test_txn');
        seekdb.connectClose(conn);
        return { passed: true, message: null };
    } catch (e) {
        return { passed: false, message: e.message };
    }
}

function testDDLOperations() {
    try {
        const conn = seekdb.connect('test', true);

        seekdb.query(conn, `
            CREATE TABLE IF NOT EXISTS test_ddl (
                id INT PRIMARY KEY,
                name VARCHAR(100),
                created_at TIMESTAMP
            )
        `);

        try {
            seekdb.query(conn, 'ALTER TABLE test_ddl ADD COLUMN description VARCHAR(255)');
        } catch (e) {
            // ALTER TABLE may not be supported
        }

        seekdb.query(conn, 'DROP TABLE IF EXISTS test_ddl');
        seekdb.connectClose(conn);
        return { passed: true, message: null };
    } catch (e) {
        return { passed: false, message: e.message };
    }
}

function testDMLOperations() {
    try {
        const conn = seekdb.connect('test', true);

        seekdb.query(conn, `
            CREATE TABLE IF NOT EXISTS test_dml (
                id INT PRIMARY KEY,
                name VARCHAR(100),
                value INT
            )
        `);

        seekdb.query(conn, "INSERT INTO test_dml VALUES (1, 'A', 10), (2, 'B', 20), (3, 'C', 30)");

        seekdb.query(conn, "UPDATE test_dml SET value = 100 WHERE id = 1");

        const result1 = seekdb.query(conn, 'SELECT value FROM test_dml WHERE id = 1');
        const rows1 = result1.fetchAll();
        if (rows1.length !== 1) {
            throw new Error('UPDATE verification failed');
        }

        seekdb.query(conn, 'DELETE FROM test_dml WHERE id = 2');

        const result2 = seekdb.query(conn, 'SELECT * FROM test_dml WHERE id = 2');
        const rows2 = result2.fetchAll();
        if (rows2.length !== 0) {
            throw new Error('DELETE verification failed');
        }

        seekdb.query(conn, 'DROP TABLE IF EXISTS test_dml');
        seekdb.connectClose(conn);
        return { passed: true, message: null };
    } catch (e) {
        return { passed: false, message: e.message };
    }
}

// Test parameterized queries (core feature)
function testParameterizedQueries() {
    let conn = null;
    try {
        conn = seekdb.connect('test', true);

        // Create test table
        seekdb.query(conn, 'CREATE TABLE IF NOT EXISTS test_params (id INT PRIMARY KEY, name VARCHAR(100))');
        seekdb.query(conn, "INSERT INTO test_params VALUES (1, 'Alice'), (2, 'Bob')");

        // Test parameterized query concept
        // Since we don't have Prepared Statement bindings yet, we test column name inference
        // which is the core feature for parameterized queries
        const result = seekdb.query(conn, "SELECT * FROM test_params WHERE id = 1");

        // Verify column names are correctly inferred (core feature: column name inference)
        if (result.columnCount !== 2) {
            throw new Error(`Expected 2 columns, got ${result.columnCount}`);
        }
        if (result.columns[0] !== 'id') {
            throw new Error(`Expected first column name 'id', got '${result.columns[0]}'`);
        }
        if (result.columns[1] !== 'name') {
            throw new Error(`Expected second column name 'name', got '${result.columns[1]}'`);
        }

        // Verify data
        const rows = result.fetchAll();
        if (rows.length !== 1) {
            throw new Error(`Expected 1 row, got ${rows.length}`);
        }
        // Convert array row to object using column names
        const rowObj = {};
        for (let i = 0; i < result.columns.length; i++) {
            rowObj[result.columns[i]] = rows[0][i];
        }
        if (rowObj.id !== '1' || rowObj.name !== 'Alice') {
            throw new Error('Parameterized query result mismatch');
        }

        seekdb.query(conn, 'DROP TABLE IF EXISTS test_params');
        seekdb.connectClose(conn);
        return { passed: true, message: null };
    } catch (e) {
        if (conn) {
            try {
                seekdb.connectClose(conn);
            } catch { }
        }
        return { passed: false, message: e.message };
    }
}

// Test VECTOR type in INSERT (regression test for VECTOR_INSERT_ISSUE)
// Ensures VECTOR columns work in INSERT without "Column cannot be null" errors
function testVectorParameterBinding() {
    let conn = null;
    try {
        conn = seekdb.connect('test', true);
        seekdb.query(conn, 'DROP TABLE IF EXISTS test_vector_params');
        seekdb.query(conn, `CREATE TABLE test_vector_params (
            id INT AUTO_INCREMENT PRIMARY KEY,
            document VARCHAR(255),
            metadata VARCHAR(255),
            embedding VECTOR(3)
        )`);
        seekdb.query(conn, `INSERT INTO test_vector_params (document, metadata, embedding) VALUES
            ('Document 1', '{"category":"A","score":95}', '[1,2,3]'),
            ('Document 2', '{"category":"B","score":90}', '[2,3,4]'),
            ('Document 3', '{"category":"A","score":88}', '[1.1,2.1,3.1]'),
            ('Document 4', '{"category":"C","score":92}', '[2.1,3.1,4.1]'),
            ('Document 5', '{"category":"B","score":85}', '[1.2,2.2,3.2]')`);
        const countResult = seekdb.query(conn, 'SELECT COUNT(*) as cnt FROM test_vector_params');
        const countRows = countResult.fetchAll();
        if (countRows.length !== 1 || countRows[0][0] !== '5') {
            seekdb.connectClose(conn);
            return { passed: false, message: `Expected 5 rows, got ${countRows[0] ? countRows[0][0] : 'no rows'}` };
        }
        const verifyResult = seekdb.query(conn, "SELECT document, embedding FROM test_vector_params WHERE document = 'Document 1' LIMIT 1");
        const verifyRows = verifyResult.fetchAll();
        if (verifyRows.length !== 1) {
            seekdb.connectClose(conn);
            return { passed: false, message: "Expected 1 row for document=Document 1" };
        }
        const embedding = verifyRows[0][1];
        if (embedding === undefined || embedding === null || (typeof embedding === 'string' && embedding.length === 0)) {
            seekdb.connectClose(conn);
            return { passed: false, message: 'Embedding column is NULL - VECTOR INSERT failed' };
        }

        // Note: VECTOR type return format
        // - Insert: JSON array format "[1,2,3]" (via direct SQL embedding)
        // - Storage: Binary format (float array, e.g., 3 floats = 12 bytes for VECTOR(3))
        // - Query return: Typically binary format (may contain non-printable characters)
        //   The important thing is that data is not NULL, which means INSERT succeeded

        // Test 2: Additional INSERT to verify VECTOR type continues to work
        seekdb.query(conn, `INSERT INTO test_vector_params (document, metadata, embedding) VALUES
            ('Auto-Detection Test', '{"category":"TEST","score":100}', '[1.5,2.5,3.5]')`);
        const countResult2 = seekdb.query(conn, 'SELECT COUNT(*) as cnt FROM test_vector_params');
        const countRows2 = countResult2.fetchAll();
        if (countRows2.length !== 1 || countRows2[0][0] !== '6') {
            seekdb.query(conn, 'DROP TABLE IF EXISTS test_vector_params');
            seekdb.connectClose(conn);
            return { passed: false, message: `Expected 6 rows after additional insert, got ${countRows2[0] ? countRows2[0][0] : 'no rows'}` };
        }
        const verifyResult2 = seekdb.query(conn, "SELECT document, embedding FROM test_vector_params WHERE document = 'Auto-Detection Test' LIMIT 1");
        const verifyRows2 = verifyResult2.fetchAll();
        if (verifyRows2.length !== 1) {
            seekdb.query(conn, 'DROP TABLE IF EXISTS test_vector_params');
            seekdb.connectClose(conn);
            return { passed: false, message: 'Expected 1 row for document=Auto-Detection Test' };
        }
        const embedding2 = verifyRows2[0][1];
        if (embedding2 === undefined || embedding2 === null || (typeof embedding2 === 'string' && embedding2.length === 0)) {
            seekdb.query(conn, 'DROP TABLE IF EXISTS test_vector_params');
            seekdb.connectClose(conn);
            return { passed: false, message: 'Embedding column is NULL in additional insert - VECTOR INSERT failed' };
        }

        seekdb.query(conn, 'DROP TABLE IF EXISTS test_vector_params');
        seekdb.connectClose(conn);
        return { passed: true, message: null };
    } catch (e) {
        if (conn) {
            try { seekdb.connectClose(conn); } catch { }
        }
        return { passed: false, message: e.message };
    }
}

// Test binary parameter binding with CAST(? AS BINARY) queries
// This test verifies that BLOB type correctly handles binary data
// in CAST(? AS BINARY) queries, ensuring proper binary comparison.
// Note: This test uses direct SQL execution as a workaround since
// prepared statement API may not be fully available.
function testBinaryParameterBinding() {
    let conn = null;
    try {
        conn = seekdb.connect('test', true);
        seekdb.query(conn, 'DROP TABLE IF EXISTS test_binary_params');
        seekdb.query(conn, 'CREATE TABLE test_binary_params (id INT PRIMARY KEY, _id VARBINARY(100))');

        // Insert binary data using direct SQL (using hex format)
        // In a full implementation, this would use prepared statements with SEEKDB_TYPE_BLOB
        const binaryValues = ['get1', 'get2', 'get3', 'get4', 'get5'];
        for (let i = 0; i < binaryValues.length; i++) {
            const hexVal = Buffer.from(binaryValues[i], 'utf-8').toString('hex');
            seekdb.query(conn, `INSERT INTO test_binary_params (id, _id) VALUES (${i + 1}, 0x${hexVal})`);
        }

        // Verify data was inserted correctly
        const countResult = seekdb.query(conn, 'SELECT COUNT(*) as cnt FROM test_binary_params');
        const countRows = countResult.fetchAll();
        // Convert array row to object using column names
        const countRowObj = {};
        for (let i = 0; i < countResult.columns.length; i++) {
            countRowObj[countResult.columns[i]] = countRows[0][i];
        }
        const cnt = countRowObj.cnt;
        if (countRows.length !== 1 || cnt !== '5') {
            seekdb.query(conn, 'DROP TABLE IF EXISTS test_binary_params');
            seekdb.connectClose(conn);
            return { passed: false, message: `Expected 5 rows, got ${cnt}` };
        }

        // Query using CAST(? AS BINARY) - using direct SQL with hex format
        const searchVal = 'get1';
        const hexSearch = Buffer.from(searchVal, 'utf-8').toString('hex');
        const selectResult = seekdb.query(conn, `SELECT _id FROM test_binary_params WHERE _id = CAST(0x${hexSearch} AS BINARY)`);
        const selectRows = selectResult.fetchAll();

        if (selectRows.length !== 1) {
            seekdb.query(conn, 'DROP TABLE IF EXISTS test_binary_params');
            seekdb.connectClose(conn);
            return { passed: false, message: 'SELECT with CAST(? AS BINARY) returned 0 rows, expected 1 row' };
        }

        // Convert array row to object using column names
        const selectRowObj = {};
        for (let i = 0; i < selectResult.columns.length; i++) {
            selectRowObj[selectResult.columns[i]] = selectRows[0][i];
        }
        const fetchedId = selectRowObj._id;
        if (fetchedId === undefined || fetchedId === null || (typeof fetchedId === 'string' && fetchedId.length === 0)) {
            seekdb.query(conn, 'DROP TABLE IF EXISTS test_binary_params');
            seekdb.connectClose(conn);
            return { passed: false, message: "Fetched _id is NULL, expected 'get1'" };
        }

        // Test negative case: query for non-existent value
        const notFoundVal = 'notfound';
        const hexNotFound = Buffer.from(notFoundVal, 'utf-8').toString('hex');
        const notFoundResult = seekdb.query(conn, `SELECT _id FROM test_binary_params WHERE _id = CAST(0x${hexNotFound} AS BINARY)`);
        const notFoundRows = notFoundResult.fetchAll();

        if (notFoundRows.length !== 0) {
            seekdb.query(conn, 'DROP TABLE IF EXISTS test_binary_params');
            seekdb.connectClose(conn);
            return { passed: false, message: `SELECT with non-existent value returned ${notFoundRows.length} rows, expected 0` };
        }

        seekdb.query(conn, 'DROP TABLE IF EXISTS test_binary_params');
        seekdb.connectClose(conn);
        return { passed: true, message: null };
    } catch (e) {
        if (conn) {
            try { seekdb.connectClose(conn); } catch { }
        }
        return { passed: false, message: e.message };
    }
}

// Test column name inference with multiple columns (core feature)
function testColumnNameInference() {
    let conn = null;
    try {
        conn = seekdb.connect('test', true);

        // Create test table with multiple columns to test column name inference
        seekdb.query(conn, 'CREATE TABLE IF NOT EXISTS test_cols (user_id INT, user_name VARCHAR(100), user_email VARCHAR(100))');
        seekdb.query(conn, "INSERT INTO test_cols VALUES (1, 'Alice', 'alice@example.com')");

        // Query with explicit column names - column names should be inferred from database
        const result = seekdb.query(conn, 'SELECT user_id, user_name, user_email FROM test_cols');

        // Verify column count
        if (result.columnCount !== 3) {
            throw new Error(`Expected 3 columns, got ${result.columnCount}`);
        }

        // Verify column names are correctly inferred
        if (result.columns.length !== 3) {
            throw new Error(`Expected 3 column names, got ${result.columns.length}`);
        }
        if (result.columns[0] !== 'user_id') {
            throw new Error(`Expected first column name 'user_id', got '${result.columns[0]}'`);
        }
        if (result.columns[1] !== 'user_name') {
            throw new Error(`Expected second column name 'user_name', got '${result.columns[1]}'`);
        }
        if (result.columns[2] !== 'user_email') {
            throw new Error(`Expected third column name 'user_email', got '${result.columns[2]}'`);
        }

        // Verify data
        const rows = result.fetchAll();
        if (rows.length !== 1) {
            throw new Error(`Expected 1 row, got ${rows.length}`);
        }

        // Verify row data can be accessed by column name
        // Convert array row to object using column names
        const rowObj = {};
        for (let i = 0; i < result.columns.length; i++) {
            rowObj[result.columns[i]] = rows[0][i];
        }
        if (rowObj.user_id !== '1') {
            throw new Error(`Expected user_id '1', got '${rowObj.user_id}'`);
        }
        if (rowObj.user_name !== 'Alice') {
            throw new Error(`Expected user_name 'Alice', got '${rowObj.user_name}'`);
        }
        if (rowObj.user_email !== 'alice@example.com') {
            throw new Error(`Expected user_email 'alice@example.com', got '${rowObj.user_email}'`);
        }

        seekdb.query(conn, 'DROP TABLE IF EXISTS test_cols');
        seekdb.connectClose(conn);
        return { passed: true, message: null };
    } catch (e) {
        if (conn) {
            try {
                seekdb.connectClose(conn);
            } catch { }
        }
        return { passed: false, message: e.message };
    }
}

function testHybridSearchGetSQL() {
    let conn = null;
    try {
        conn = seekdb.connect('test', true);

        try {
            seekdb.query(conn, 'DROP TABLE IF EXISTS doc_table');
        } catch (e) {
            // Ignore
        }

        const createTableSQL = `CREATE TABLE doc_table (
            c1 INT,
            vector VECTOR(3),
            query VARCHAR(255),
            content VARCHAR(255),
            VECTOR INDEX idx1(vector) WITH (distance=l2, type=hnsw, lib=vsag),
            FULLTEXT idx2(query),
            FULLTEXT idx3(content)
        )`;
        seekdb.query(conn, createTableSQL);

        const insertSQL = `INSERT INTO doc_table VALUES
            (1, '[1,2,3]', 'hello world', 'oceanbase Elasticsearch database'),
            (2, '[1,2,1]', 'hello world, what is your name', 'oceanbase mysql database'),
            (3, '[1,1,1]', 'hello world, how are you', 'oceanbase oracle database'),
            (4, '[1,3,1]', 'real world, where are you from', 'postgres oracle database'),
            (5, '[1,3,2]', 'real world, how old are you', 'redis oracle database'),
            (6, '[2,1,1]', 'hello world, where are you from', 'starrocks oceanbase database')`;
        seekdb.query(conn, insertSQL);

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

        const setParmSQL = `SET @parm = '${escapedParams}'`;
        seekdb.query(conn, setParmSQL);

        const getSqlQuery = `SELECT DBMS_HYBRID_SEARCH.GET_SQL('doc_table', @parm)`;
        const sqlResult = seekdb.query(conn, getSqlQuery);
        const sqlRows = sqlResult.fetchAll();

        if (sqlRows.length === 0) {
            throw new Error('GET_SQL returned no rows');
        }

        seekdb.connectClose(conn);
        return { passed: true, message: null };
    } catch (e) {
        if (conn) {
            try {
                seekdb.connectClose(conn);
            } catch { }
        }
        return { passed: false, message: e.message };
    }
}

function testHybridSearchSearch() {
    let conn = null;
    try {
        conn = seekdb.connect('test', true);

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
        const searchResult = seekdb.query(conn, searchQuery);
        const searchRows = searchResult.fetchAll();

        if (searchRows.length === 0) {
            throw new Error('SEARCH returned no rows');
        }

        seekdb.connectClose(conn);
        return { passed: true, message: null };
    } catch (e) {
        if (conn) {
            try {
                seekdb.connectClose(conn);
            } catch { }
        }
        return { passed: false, message: e.message };
    }
}

// Main test runner
function runAllTests() {
    console.log('='.repeat(70));
    console.log('SeekDB Node.js N-API Binding Test Suite');
    console.log('='.repeat(70));
    console.log('');

    const dbDir = process.argv[2] || './seekdb.db';
    console.log(`[seekdb-napi] opening database directory: ${dbDir}`);
    try {
        seekdb.open(dbDir);
    } catch (e) {
        console.error(`::error::Failed to open database: ${e.message}`);
        return 1;
    }
    console.log('[seekdb-napi] seekdb.open OK');

    const skipHeavy = process.env.SEEKDB_NODE_NAPI_SKIP_HEAVY === '1'
        || /^true$/i.test(process.env.SEEKDB_NODE_NAPI_SKIP_HEAVY || '');
    if (skipHeavy) {
        console.log('[seekdb-napi] SEEKDB_NODE_NAPI_SKIP_HEAVY set — skipping VECTOR + hybrid search tests');
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
    ].filter((tc) => {
        if (!skipHeavy) {
            return true;
        }
        return tc.name !== 'VECTOR Parameter Binding'
            && tc.name !== 'DBMS_HYBRID_SEARCH.GET_SQL'
            && tc.name !== 'DBMS_HYBRID_SEARCH.SEARCH';
    });

    const results = [];
    const failedTests = [];

    try {
        for (const testCase of testCases) {
            process.stdout.write(`[TEST] ${testCase.name.padEnd(40)} ... `);
            const result = testCase.fn();
            results.push({ name: testCase.name, ...result });

            if (result.passed) {
                console.log('PASS');
            } else {
                console.log('FAIL');
                failedTests.push({ name: testCase.name, message: result.message });
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

        // Same-directory absolute open before close (aligned with python test.py; avoids a second node process on Windows).
        if (passed === total) {
            const absSame = path.resolve(dbDir);
            try {
                seekdb.open(absSame);
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
        console.error(`::error::Unexpected error during tests: ${e.message}`);
        console.error(e.stack);
        return 1;
    }
}

// Run tests
try {
    exitAfterBindingTests(runAllTests());
} catch (error) {
    console.error('::error::Fatal error:', error.message);
    exitAfterBindingTests(1);
}
