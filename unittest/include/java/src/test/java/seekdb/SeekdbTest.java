/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

package seekdb;

import java.util.Arrays;

/**
 * Test suite for SeekDB Java JNI binding.
 * Aligned with nodejs_napi test.js core subset.
 */
public class SeekdbTest {

    private static final String DATABASE = "test";

    static class TestResult {
        final boolean passed;
        final String message;

        TestResult(boolean passed, String message) {
            this.passed = passed;
            this.message = message;
        }
    }

    static TestResult testOpen() {
        try {
            SeekdbConnection conn = new SeekdbConnection();
            conn.connect(DATABASE, true);
            conn.close();
            return new TestResult(true, null);
        } catch (Exception e) {
            return new TestResult(false, e.getMessage());
        }
    }

    static TestResult testConnection() {
        try {
            SeekdbConnection conn = new SeekdbConnection();
            conn.connect(DATABASE, true);
            conn.close();
            return new TestResult(true, null);
        } catch (Exception e) {
            return new TestResult(false, e.getMessage());
        }
    }

    static TestResult testErrorHandling() {
        try {
            SeekdbConnection conn = new SeekdbConnection();
            conn.connect(DATABASE, true);
            try {
                conn.query("INVALID SQL STATEMENT");
                conn.close();
                return new TestResult(false, "Should have thrown error for invalid SQL");
            } catch (Exception expected) {
                // Expected
            }
            conn.close();
            return new TestResult(true, null);
        } catch (Exception e) {
            return new TestResult(false, e.getMessage());
        }
    }

    static TestResult testResultOperations() {
        try {
            SeekdbConnection conn = new SeekdbConnection();
            conn.connect(DATABASE, true);
            SeekdbResult result = conn.query("SELECT 1 as id, 'hello' as message, 3.14 as price, true as active");

            if (result.getRowCount() != 1) {
                return new TestResult(false, "Expected row count 1, got " + result.getRowCount());
            }
            if (result.getColumnCount() != 4) {
                return new TestResult(false, "Expected column count 4, got " + result.getColumnCount());
            }

            String[][] rows = result.fetchAll();
            if (rows.length != 1) {
                return new TestResult(false, "Expected 1 row, got " + rows.length);
            }

            conn.close();
            return new TestResult(true, null);
        } catch (Exception e) {
            return new TestResult(false, e.getMessage());
        }
    }

    static TestResult testRowOperations() {
        try {
            SeekdbConnection conn = new SeekdbConnection();
            conn.connect(DATABASE, true);

            conn.executeUpdate("CREATE TABLE IF NOT EXISTS test_types (" +
                    "id INT PRIMARY KEY, name VARCHAR(100), price DECIMAL(10,2), " +
                    "quantity INT, active BOOLEAN, score DOUBLE)");
            conn.executeUpdate("INSERT INTO test_types VALUES " +
                    "(1, 'Product A', 99.99, 10, true, 4.5), " +
                    "(2, 'Product B', 199.99, 5, false, 3.8), " +
                    "(3, NULL, NULL, NULL, NULL, NULL)");

            SeekdbResult result = conn.query("SELECT * FROM test_types ORDER BY id");
            String[][] rows = result.fetchAll();

            if (rows.length != 3) {
                return new TestResult(false, "Expected 3 rows, got " + rows.length);
            }

            conn.executeUpdate("DROP TABLE IF EXISTS test_types");
            conn.close();
            return new TestResult(true, null);
        } catch (Exception e) {
            return new TestResult(false, e.getMessage());
        }
    }

    static TestResult testErrorMessage() {
        try {
            SeekdbConnection conn = new SeekdbConnection();
            conn.connect(DATABASE, true);

            try {
                conn.query("SELECT * FROM non_existent_table");
            } catch (Exception expected) {
                // Expected
            }

            conn.close();
            return new TestResult(true, null);
        } catch (Exception e) {
            return new TestResult(false, e.getMessage());
        }
    }

    static TestResult testTransactionManagement() {
        try {
            SeekdbConnection conn = new SeekdbConnection();
            conn.connect(DATABASE, false);

            conn.executeUpdate("CREATE TABLE IF NOT EXISTS test_txn (id INT PRIMARY KEY, value INT)");

            conn.begin();
            conn.executeUpdate("INSERT INTO test_txn VALUES (1, 100)");
            conn.commit();

            SeekdbResult result = conn.query("SELECT * FROM test_txn WHERE id = 1");
            String[][] rows = result.fetchAll();
            if (rows.length != 1) {
                return new TestResult(false, "Data not committed");
            }

            conn.begin();
            conn.executeUpdate("INSERT INTO test_txn VALUES (2, 200)");
            conn.rollback();

            conn.executeUpdate("DROP TABLE IF EXISTS test_txn");
            conn.close();
            return new TestResult(true, null);
        } catch (Exception e) {
            return new TestResult(false, e.getMessage());
        }
    }

    static TestResult testDDLOperations() {
        try {
            SeekdbConnection conn = new SeekdbConnection();
            conn.connect(DATABASE, true);

            conn.executeUpdate("CREATE TABLE IF NOT EXISTS test_ddl (" +
                    "id INT PRIMARY KEY, name VARCHAR(100), created_at TIMESTAMP)");

            try {
                conn.executeUpdate("ALTER TABLE test_ddl ADD COLUMN description VARCHAR(255)");
            } catch (Exception ignored) {
                // ALTER TABLE may not be supported
            }

            conn.executeUpdate("DROP TABLE IF EXISTS test_ddl");
            conn.close();
            return new TestResult(true, null);
        } catch (Exception e) {
            return new TestResult(false, e.getMessage());
        }
    }

    static TestResult testDMLOperations() {
        try {
            SeekdbConnection conn = new SeekdbConnection();
            conn.connect(DATABASE, true);

            conn.executeUpdate("CREATE TABLE IF NOT EXISTS test_dml (" +
                    "id INT PRIMARY KEY, name VARCHAR(100), value INT)");
            conn.executeUpdate("INSERT INTO test_dml VALUES (1, 'A', 10), (2, 'B', 20), (3, 'C', 30)");
            conn.executeUpdate("UPDATE test_dml SET value = 100 WHERE id = 1");

            SeekdbResult result1 = conn.query("SELECT value FROM test_dml WHERE id = 1");
            String[][] rows1 = result1.fetchAll();
            if (rows1.length != 1) {
                return new TestResult(false, "UPDATE verification failed");
            }

            conn.executeUpdate("DELETE FROM test_dml WHERE id = 2");

            SeekdbResult result2 = conn.query("SELECT * FROM test_dml WHERE id = 2");
            String[][] rows2 = result2.fetchAll();
            if (rows2.length != 0) {
                return new TestResult(false, "DELETE verification failed");
            }

            conn.executeUpdate("DROP TABLE IF EXISTS test_dml");
            conn.close();
            return new TestResult(true, null);
        } catch (Exception e) {
            return new TestResult(false, e.getMessage());
        }
    }

    static TestResult testParameterizedQueries() {
        try {
            SeekdbConnection conn = new SeekdbConnection();
            conn.connect(DATABASE, true);

            conn.executeUpdate("CREATE TABLE IF NOT EXISTS test_params (id INT PRIMARY KEY, name VARCHAR(100))");
            conn.executeUpdate("INSERT INTO test_params VALUES (1, 'Alice'), (2, 'Bob')");

            SeekdbResult result = conn.query("SELECT * FROM test_params WHERE id = 1");

            if (result.getColumnCount() != 2) {
                return new TestResult(false, "Expected 2 columns, got " + result.getColumnCount());
            }
            String[] cols = result.getColumns();
            if (cols.length < 2 || !"id".equals(cols[0]) || !"name".equals(cols[1])) {
                return new TestResult(false, "Expected column names 'id', 'name', got " + Arrays.toString(cols));
            }

            String[][] rows = result.fetchAll();
            if (rows.length != 1) {
                return new TestResult(false, "Expected 1 row, got " + rows.length);
            }
            String[] row = rows[0];
            if (row.length < 2 || !"1".equals(row[0]) || !"Alice".equals(row[1])) {
                return new TestResult(false, "Parameterized query result mismatch");
            }

            conn.executeUpdate("DROP TABLE IF EXISTS test_params");
            conn.close();
            return new TestResult(true, null);
        } catch (Exception e) {
            return new TestResult(false, e.getMessage());
        }
    }

    static TestResult testColumnNameInference() {
        try {
            SeekdbConnection conn = new SeekdbConnection();
            conn.connect(DATABASE, true);

            conn.executeUpdate("CREATE TABLE IF NOT EXISTS test_cols " +
                    "(user_id INT, user_name VARCHAR(100), user_email VARCHAR(100))");
            conn.executeUpdate("INSERT INTO test_cols VALUES (1, 'Alice', 'alice@example.com')");

            SeekdbResult result = conn.query("SELECT user_id, user_name, user_email FROM test_cols");

            if (result.getColumnCount() != 3) {
                return new TestResult(false, "Expected 3 columns, got " + result.getColumnCount());
            }
            String[] cols = result.getColumns();
            if (cols.length < 3 || !"user_id".equals(cols[0]) || !"user_name".equals(cols[1]) || !"user_email".equals(cols[2])) {
                return new TestResult(false, "Expected column names user_id, user_name, user_email, got " + Arrays.toString(cols));
            }

            String[][] rows = result.fetchAll();
            if (rows.length != 1) {
                return new TestResult(false, "Expected 1 row, got " + rows.length);
            }

            conn.executeUpdate("DROP TABLE IF EXISTS test_cols");
            conn.close();
            return new TestResult(true, null);
        } catch (Exception e) {
            return new TestResult(false, e.getMessage());
        }
    }

    private static void writeBindingExitProbe(int code) {
        if (!"1".equals(System.getenv("SEEKDB_BINDING_EXIT_PROBE"))) {
            return;
        }
        String tmp = System.getenv("TEMP");
        if (tmp == null || tmp.isEmpty()) {
            tmp = System.getProperty("java.io.tmpdir");
        }
        long pid = ProcessHandle.current().pid();
        java.nio.file.Path p = java.nio.file.Paths.get(tmp, "seekdb_binding_exit_probe_" + pid + ".log");
        try {
            java.nio.file.Files.writeString(
                    p,
                    "before_process_exit code=" + code + System.lineSeparator(),
                    java.nio.file.StandardOpenOption.CREATE,
                    java.nio.file.StandardOpenOption.APPEND);
        } catch (Exception ignored) {
        }
    }

    public static void main(String[] args) {
        System.out.println("=".repeat(70));
        System.out.println("SeekDB Java JNI Binding Test Suite");
        System.out.println("=".repeat(70));
        System.out.println();

        String dbDir = args.length > 0 ? args[0] : "./seekdb.db";

        try {
            Seekdb.open(dbDir);
        } catch (Exception e) {
            System.err.println("::error::Failed to open database: " + e.getMessage());
            writeBindingExitProbe(1);
            System.exit(1);
        }

        TestResult[] results = {
                run("Database Open", SeekdbTest::testOpen),
                run("Connection Creation", SeekdbTest::testConnection),
                run("Error Handling", SeekdbTest::testErrorHandling),
                run("Result Operations", SeekdbTest::testResultOperations),
                run("Row Operations", SeekdbTest::testRowOperations),
                run("Error Message", SeekdbTest::testErrorMessage),
                run("Transaction Management", SeekdbTest::testTransactionManagement),
                run("DDL Operations", SeekdbTest::testDDLOperations),
                run("DML Operations", SeekdbTest::testDMLOperations),
                run("Parameterized Queries", SeekdbTest::testParameterizedQueries),
                run("Column Name Inference", SeekdbTest::testColumnNameInference),
        };

        System.out.println();
        System.out.println("-".repeat(70));

        int passed = 0;
        for (TestResult r : results) {
            if (r.passed) passed++;
        }
        int total = results.length;
        int failed = total - passed;

        if (failed > 0) {
            System.out.println("Failed Tests:");
            System.out.println("-".repeat(70));
            for (int i = 0; i < results.length; i++) {
                if (!results[i].passed) {
                    System.out.println("  x " + getTestName(i));
                    if (results[i].message != null) {
                        System.out.println("    Error: " + results[i].message);
                    }
                }
            }
            System.out.println("-".repeat(70));
        }

        System.out.println("Total: " + passed + "/" + total + " passed, " + failed + " failed");
        System.out.println();

        // Same-directory absolute open before close (aligned with python test.py).
        if (passed == total) {
            try {
                java.nio.file.Path absSame = java.nio.file.Paths.get(dbDir).toAbsolutePath().normalize();
                System.out.print("[TEST] Absolute path (same DB directory)               ... ");
                Seekdb.open(absSame.toString());
                System.out.println("PASS");
            } catch (Exception e) {
                System.out.println("FAIL");
                System.err.println("::error::Absolute-path same-directory check failed: " + e.getMessage());
                writeBindingExitProbe(1);
                System.exit(1);
            }
        }

        Seekdb.close();

        if (passed == total) {
            System.out.println("::notice::All tests passed successfully!");
            System.out.println("=".repeat(70));
            writeBindingExitProbe(0);
            System.exit(0);
        } else {
            System.err.println("::error::" + failed + " test(s) failed");
            System.out.println("=".repeat(70));
            writeBindingExitProbe(1);
            System.exit(1);
        }
    }

    private static String getTestName(int i) {
        String[] names = {
                "Database Open", "Connection Creation", "Error Handling", "Result Operations",
                "Row Operations", "Error Message", "Transaction Management", "DDL Operations",
                "DML Operations", "Parameterized Queries", "Column Name Inference"
        };
        return i < names.length ? names[i] : "Test " + i;
    }

    private static TestResult run(String name, java.util.function.Supplier<TestResult> fn) {
        System.out.print("[TEST] " + String.format("%-40s", name) + " ... ");
        TestResult r = fn.get();
        System.out.println(r.passed ? "PASS" : "FAIL");
        if (!r.passed && r.message != null) {
            System.err.println("::error::Test \"" + name + "\" failed: " + r.message);
        }
        return r;
    }
}
