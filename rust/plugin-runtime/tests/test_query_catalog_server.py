#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
"""Offline acceptance-runner checks, NOT query catalog/server correctness tests."""
import contextlib
import io
import unittest
from types import SimpleNamespace
from unittest.mock import patch

import query_catalog_server as suite


class DatabaseError(Exception):
    pass


class Cursor:
    def __init__(self, results=((),), error=None, late_error=None):
        self.results = results
        self.index = 0
        self.error = error
        self.late_error = late_error
        self.drained = []
        self.closed = False
        self.statement = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.closed = True

    def execute(self, statement, arguments):
        self.statement = (statement, arguments)
        if self.error:
            raise self.error

    def fetchall(self):
        self.drained.append(self.index)
        return self.results[self.index]

    def nextset(self):
        if self.late_error:
            raise self.late_error
        self.index += 1
        return self.index < len(self.results)


class Connection:
    def __init__(self, cursor=None):
        self.result = cursor or Cursor()
        self.rolled_back = False
        self.closed = False

    def cursor(self):
        return self.result

    def select_db(self, database):
        self.database = database

    def rollback(self):
        self.rolled_back = True

    def close(self):
        self.closed = True


class HelpersTest(unittest.TestCase):
    def test_autocommit_visibility_failure_reports_both_ids_without_retry(self):
        for observed in (None, 99):
            with self.subTest(observed=observed), patch.object(suite, "mutate", return_value=42), \
                    patch.object(suite, "lookup", return_value=observed) as lookup:
                with self.assertRaisesRegex(AssertionError, f"created=42, observed={observed}"):
                    suite.check_autocommit(object(), object(), DatabaseError)
                self.assertEqual(lookup.call_count, 1)

    def test_call_drains_every_result(self):
        cursor = Cursor(( ((42,),), (), ((99,),) ))
        self.assertEqual(suite.scalar(Connection(cursor), "CALL p()"), 42)
        self.assertEqual(cursor.drained, [0, 1, 2])
        self.assertTrue(cursor.closed)

    def test_late_result_error_is_not_success(self):
        cursor = Cursor((((42,),),), late_error=DatabaseError(2013, "lost connection"))
        with self.assertRaises(DatabaseError):
            suite.scalar(Connection(cursor), "CALL p()")
        self.assertTrue(cursor.closed)

    def test_scalar_shape(self):
        self.assertIsNone(suite.scalar(Connection(), "CREATE FUNCTION f()"))
        self.assertIsNone(suite.scalar(Connection(Cursor((((None,),),))), "SELECT NULL"))
        for rows in (((1,), (2,)), ((1, 2),)):
            with self.subTest(rows=rows), self.assertRaises(AssertionError):
                suite.scalar(Connection(Cursor((rows,))), "SELECT ambiguous")

    def test_sql_payload_is_bound_not_interpolated(self):
        cursor = Cursor((((700,),),))
        statement = "CREATE FUNCTION f() RETURNS BIGINT RETURN 1 /* ' ; 中 */"
        self.assertEqual(suite.mutate(Connection(cursor), statement), 700)
        self.assertEqual(cursor.statement, (suite.MUTATE, (statement,)))

    def test_show_create_uses_actual_definition_column(self):
        cursor = Cursor(((("f", "STRICT_ALL_TABLES", b"CREATE FUNCTION f() RETURN 1"),),))
        self.assertEqual(suite.function_definition(Connection(cursor), "f"), "CREATE FUNCTION f() RETURN 1")
        with self.assertRaises(AssertionError):
            suite.function_definition(Connection(), "f")

    def test_object_id_rejects_fake_success(self):
        for value in (None, False, True, 0, -1, 1 << 63, 1.0, "7"):
            with self.subTest(value=value), self.assertRaises(AssertionError):
                suite.object_id(value)
        self.assertEqual(suite.object_id((1 << 63) - 1), (1 << 63) - 1)

    def test_negative_case_requires_exact_error(self):
        connection = Connection(Cursor(error=DatabaseError(1304, "duplicate")))
        self.assertEqual(suite.expect_error(connection, "SQL", None, {1304}, DatabaseError), 1304)
        for code in (1064, 1305, 4012, 2002, 2003, 2006, 2013, 2055):
            with self.subTest(code=code), self.assertRaises(DatabaseError):
                suite.expect_error(Connection(Cursor(error=DatabaseError(code, "wrong failure"))),
                                   "SQL", None, {1304}, DatabaseError)

    def test_unexpected_success_and_empty_oracle_fail(self):
        with self.assertRaises(AssertionError):
            suite.expect_error(Connection(), "SQL", None, {1304}, DatabaseError)
        with self.assertRaises(AssertionError):
            suite.expect_error(Connection(), "SQL", None, set(), DatabaseError)

    def test_non_database_failure_is_not_accepted(self):
        with self.assertRaises(ValueError):
            suite.expect_error(Connection(Cursor(error=ValueError(1304))), "SQL", None, {1304}, DatabaseError)

    def test_identifiers_are_bounded_generated_names(self):
        self.assertEqual(suite.identifier("query_catalog_abc123"), "`query_catalog_abc123`")
        for name in ("", "x` DROP DATABASE mysql", "a.b", "a" * 65, "海洋", "1abc", "a\nb"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                suite.identifier(name)

    def test_cli_requires_explicit_disposable_confirmation(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            suite.parse_options(["--port", "2881"])
        options = suite.parse_options(["--port", "2881", "--confirm-disposable-server"])
        self.assertEqual(options.port, 2881)
        for port in ("0", "65536", "-1", "host:2881"):
            with self.subTest(port=port), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                suite.parse_options(["--port", port, "--confirm-disposable-server"])

    def test_checks_survive_python_optimization(self):
        # require(), not Python assert, is the runner's correctness primitive.
        with self.assertRaises(AssertionError):
            suite.require(False, "must fail even under python -O")


class RunnerControlTest(unittest.TestCase):
    """Only resource ownership and reporting. Every database check is stubbed."""

    def setUp(self):
        self.calls = []
        self.connections = []
        self.configs = []
        self.output = io.StringIO()
        self.errors = io.StringIO()
        self.database = "query_catalog_" + "a" * 20
        self.sql_failure = None

        def connect(**config):
            self.configs.append(config)
            connection = Connection()
            self.connections.append(connection)
            return connection

        def scalar(connection, sql, args=None):
            self.calls.append((sql, args))
            # PyMySQL applies Python percent-formatting whenever args is not
            # None. Preserve that constraint even in this dependency-free stub.
            if args is not None:
                sql % tuple(repr(value) for value in args)
            if self.sql_failure and self.sql_failure in sql:
                raise DatabaseError(9001, "injected runner transport failure")
            if "automatic_sp_privileges" in sql:
                return 1
            if "SELECT database_id" in sql:
                return 123
            if "SELECT DATABASE()" in sql:
                return self.database
            if "COUNT(*)" in sql:
                return 0
            return None

        self.driver = SimpleNamespace(connect=connect, MySQLError=DatabaseError)
        self.stack = contextlib.ExitStack()
        self.addCleanup(self.stack.close)
        self.stack.enter_context(patch.object(suite, "scalar", side_effect=scalar))
        self.stack.enter_context(patch.object(suite.secrets, "token_hex", return_value="a" * 20))
        self.stack.enter_context(patch.object(suite.secrets, "token_urlsafe", return_value="not-logged-password"))
        self.phases = []
        for name in ("check_autocommit", "check_caller_transaction", "check_savepoints",
                     "check_statement_rollback", "check_invoker", "check_rejections"):
            self.phases.append(self.stack.enter_context(patch.object(suite, name)))
        self.stack.enter_context(contextlib.redirect_stdout(self.output))
        self.stack.enter_context(contextlib.redirect_stderr(self.errors))

    def run_suite(self):
        suite.run(SimpleNamespace(port=2881, user="fixture_admin"), self.driver)

    def test_success_closes_participants_and_cleans_only_created_resources(self):
        self.run_suite()
        self.assertTrue(all(connection.closed for connection in self.connections))
        self.assertTrue(all(connection.rolled_back for connection in self.connections[1:]))
        self.assertTrue(all(phase.call_count == 1 for phase in self.phases))
        drops = [sql for sql, _ in self.calls if sql.startswith("DROP")]
        self.assertEqual(drops, ["DROP USER 'qc_read_" + "a" * 20 + "'@'%'",
                                 "DROP USER 'qc_make_" + "a" * 20 + "'@'%'",
                                 f"DROP DATABASE `{self.database}`"])
        self.assertIn("PASS:", self.output.getvalue())
        self.assertNotIn("not-logged-password", self.output.getvalue() + self.errors.getvalue())
        self.assertTrue(all(config["host"] == "127.0.0.1" for config in self.configs))
        self.assertFalse(any("SET GLOBAL" in sql or "INSTALL PLUGIN" in sql for sql, _ in self.calls))

    def test_server_failure_retains_fixtures_and_never_reports_pass(self):
        self.phases[1].side_effect = DatabaseError(2013, "lost server")
        with self.assertRaises(DatabaseError):
            self.run_suite()
        self.assertFalse(any(sql.startswith("DROP") for sql, _ in self.calls))
        self.assertNotIn("PASS:", self.output.getvalue())
        self.assertIn(self.database, self.errors.getvalue())
        self.assertTrue(all(connection.closed for connection in self.connections))
        self.assertEqual(self.phases[2].call_count, 0)

    def test_setup_sql_with_real_pymysql_mogrify_without_network(self):
        try:
            import pymysql
        except ImportError:
            self.skipTest("optional PyMySQL driver is not installed")
        self.run_suite()
        connection = pymysql.connections.Connection(defer_connect=True)
        # No handshake takes place; use ordinary backslash-escaping SQL mode.
        connection.server_status = 0
        rendered = []
        with connection.cursor() as cursor:
            for sql, args in self.calls:
                rendered.append(cursor.mogrify(sql, args))
        creates = [sql for sql in rendered if sql.startswith("CREATE USER")]
        self.assertEqual(creates, [
            f"CREATE USER '{name}'@'%' IDENTIFIED BY 'not-logged-password'"
            for name in ("qc_read_" + "a" * 20, "qc_make_" + "a" * 20)
        ])
        self.assertTrue(all("@'%%'" not in sql for sql in rendered))

    def test_missing_plugin_fails_before_creating_resources(self):
        self.sql_failure = "seekdb_rust_routine_ddl"
        with self.assertRaises(DatabaseError):
            self.run_suite()
        self.assertFalse(any(sql.startswith("CREATE") for sql, _ in self.calls))
        self.assertEqual(len(self.connections), 1)
        self.assertTrue(self.connections[0].closed)
        self.assertNotIn("PASS:", self.output.getvalue())

    def test_setup_failure_retains_only_known_fixture_names(self):
        self.sql_failure = "GRANT SELECT, CREATE ROUTINE"
        with self.assertRaises(DatabaseError):
            self.run_suite()
        self.assertIn(self.database, self.errors.getvalue())
        self.assertFalse(any(sql.startswith("DROP") for sql, _ in self.calls))
        self.assertTrue(all(connection.closed for connection in self.connections))

    def test_cleanup_failure_is_not_a_success(self):
        self.sql_failure = "DROP USER"
        with self.assertRaises(DatabaseError):
            self.run_suite()
        self.assertNotIn("PASS:", self.output.getvalue())
        self.assertTrue(all(connection.closed for connection in self.connections))
        self.assertIn(self.database, self.errors.getvalue())

    def test_cleanup_failure_preserves_original_server_error(self):
        primary = DatabaseError(2013, "primary server failure")

        def fail(*args):
            self.connections[0].close = lambda: (_ for _ in ()).throw(ValueError("close failed"))
            raise primary

        self.phases[0].side_effect = fail
        with self.assertRaises(DatabaseError) as caught:
            self.run_suite()
        self.assertIs(caught.exception, primary)
        self.assertIn("cleanup failed", self.errors.getvalue())
        self.assertNotIn("PASS:", self.output.getvalue())


if __name__ == "__main__":
    unittest.main()
