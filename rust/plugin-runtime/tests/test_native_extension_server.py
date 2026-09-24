#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Offline runner safeguards only; does NOT verify server/catalog correctness."""
import contextlib
import io
import unittest
from types import SimpleNamespace
from unittest.mock import patch

import native_extension_server as suite
from test_query_catalog_server import Connection, Cursor, DatabaseError

DRIVER = SimpleNamespace(MySQLError=DatabaseError)


def bindings(updated=False):
    rows = [("native_add_one", 101, suite.MODULE, suite.IMPLEMENTATION, 1),
            ("native_increment", 102, suite.MODULE, suite.UNNAMED, 1)]
    if updated:
        rows.append(("native_successor", 103, suite.MODULE, suite.IMPLEMENTATION, 1))
    return rows


def replaced_bindings():
    return [(name, 104 if name == "native_increment" else identity, module, implementation, abi)
            for name, identity, module, implementation, abi in bindings(True)]


class HelpersTest(unittest.TestCase):
    def test_cli_requires_confirmation_and_one_local_endpoint(self):
        for arguments in ([], ["--port", "2881"], ["--confirm-disposable-server"],
                          ["--port", "2881", "--unix-socket", "/tmp/sql.sock", "--confirm-disposable-server"],
                          ["--unix-socket", "relative.sock", "--confirm-disposable-server"]):
            with self.subTest(arguments=arguments), contextlib.redirect_stderr(io.StringIO()), \
                    self.assertRaises(SystemExit):
                suite.parse_options(arguments)
        for port in ("0", "-1", "65536", "host:2881"):
            with self.subTest(port=port), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                suite.parse_options(["--port", port, "--confirm-disposable-server"])
        self.assertEqual(suite.parse_options(["--port", "2881", "--confirm-disposable-server"]).port, 2881)
        self.assertEqual(suite.parse_options(["--unix-socket", "/tmp/sql.sock", "--confirm-disposable-server"])
                         .unix_socket, "/tmp/sql.sock")

    def test_checks_are_not_python_assert_statements(self):
        with self.assertRaises(AssertionError):
            suite.require(False, "also fails under python -O")

    def test_query_binds_and_drains_all_results(self):
        cursor = Cursor(( ((42,),), (), ((99,),) ))
        self.assertEqual(suite.scalar(Connection(cursor), "SELECT f(%s)", ("'quoted'",)), 42)
        self.assertEqual(cursor.statement, ("SELECT f(%s)", ("'quoted'",)))
        self.assertEqual(cursor.drained, [0, 1, 2])
        self.assertTrue(cursor.closed)

    def test_late_result_failure_propagates(self):
        cursor = Cursor((((42,),),), late_error=DatabaseError(2013, "lost connection"))
        with self.assertRaises(DatabaseError):
            suite.scalar(Connection(cursor), "SQL")
        self.assertTrue(cursor.closed)

    def test_scalar_rejects_ambiguous_shape(self):
        self.assertIsNone(suite.scalar(Connection(), "DDL"))
        self.assertIsNone(suite.scalar(Connection(Cursor((((None,),),))), "SELECT NULL"))
        for rows in (((1,), (2,)), ((1, 2),)):
            with self.subTest(rows=rows), self.assertRaises(AssertionError):
                suite.scalar(Connection(Cursor((rows,))), "SQL")

    def test_rejection_requires_expected_error(self):
        suite.reject(Connection(Cursor(error=DatabaseError(4179, "member"))), "DROP FUNCTION f", DRIVER, {4179})
        for code in (1064, 1146, 2002, 2003, 2006, 2013, 2055, 4016, 4109):
            with self.subTest(code=code), self.assertRaises(AssertionError):
                suite.reject(Connection(Cursor(error=DatabaseError(code, "wrong error"))), "SQL", DRIVER, {4179})
        for codes in (set(), {4179}):
            with self.assertRaises(AssertionError):
                suite.reject(Connection(), "unexpected success", DRIVER, codes)
        with self.assertRaises(ValueError):
            suite.reject(Connection(Cursor(error=ValueError(4179))), "SQL", DRIVER, {4179})

    def test_native_bindings_require_exact_name_module_implementation_and_abi(self):
        suite.check_bindings(bindings())
        suite.check_bindings(bindings(True), True)
        for index, value in ((0, "wrong_name"), (1, None), (1, False), (1, 0), (1, -1),
                             (1, "101"), (1, 1 << 63), (1, 102), (2, "wrong.module"),
                             (3, suite.UNNAMED), (4, 2)):
            rows = bindings()
            changed = list(rows[0])
            changed[index] = value
            rows[0] = tuple(changed)
            with self.subTest(index=index, value=value), self.assertRaises(AssertionError):
                suite.check_bindings(rows)
        with self.assertRaises(AssertionError):
            suite.check_bindings(bindings(), True)

    def test_membership_checks_identity_not_only_count(self):
        with patch.object(suite, "query", return_value=((101,), (102,))) as query:
            suite.check_members(object(), 7, 8, bindings())
            self.assertEqual(query.call_args.args[2], (7, 8))
        for rows in (((101,), (999,)), ((101,), (101,)), (), ((101,), (102,), (103,))):
            with self.subTest(rows=rows), patch.object(suite, "query", return_value=rows), \
                    self.assertRaises(AssertionError):
                suite.check_members(object(), 7, 8, bindings())
        with patch.object(suite, "query", return_value=()):
            suite.check_members(object(), 7, 8, ())

    def test_replacement_requires_new_identity_and_unchanged_other_members(self):
        self.assertEqual(suite.check_replacement(bindings(True), replaced_bindings()), bindings(True)[1])
        for rows in (bindings(True), bindings(),
                     [(name, identity + 1000, module, implementation, abi)
                      for name, identity, module, implementation, abi in replaced_bindings()]):
            with self.subTest(rows=rows), self.assertRaises(AssertionError):
                suite.check_replacement(bindings(True), rows)

    def test_removed_acl_check_is_identity_scoped_and_requires_zero(self):
        with patch.object(suite, "scalar", return_value=0) as scalar:
            suite.check_no_acl(object(), 102)
            self.assertEqual(scalar.call_args.args[2], (102,))
            self.assertIn("tenant_id=1 AND objtype=9 AND obj_id=%s", scalar.call_args.args[1])
        for count in (None, 1, 2):
            with self.subTest(count=count), patch.object(suite, "scalar", return_value=count), \
                    self.assertRaises(AssertionError):
                suite.check_no_acl(object(), 102)

    def test_dependency_identity_is_bound_and_count_is_exact(self):
        for present in (True, False):
            with patch.object(suite, "scalar", return_value=int(present)) as scalar:
                suite.check_edges(object(), bindings(), present)
                self.assertEqual([call.args[2] for call in scalar.call_args_list],
                                 [("routine.101", suite.MODULE, suite.IMPLEMENTATION),
                                  ("routine.102", suite.MODULE, suite.UNNAMED)])
            for count in (None, 2, int(not present)):
                with patch.object(suite, "scalar", return_value=count), self.assertRaises(AssertionError):
                    suite.check_edges(object(), bindings(), present)

    def test_value_checks_distinguish_default_and_explicit_null(self):
        def scalar(connection, sql, args=None):
            return 42 if args is None else (None if args[0] is None else args[0] + 1)

        with patch.object(suite, "scalar", side_effect=scalar) as calls:
            suite.check_values(object(), True)
            self.assertIn("SELECT native_increment()", [call.args[1] for call in calls.call_args_list])
            self.assertEqual(sum(call.args[2] == (None,) for call in calls.call_args_list if len(call.args) == 3), 3)
        with patch.object(suite, "scalar", return_value=None), self.assertRaisesRegex(AssertionError, "default argument"):
            suite.check_values(object())
        with patch.object(suite, "scalar", side_effect=scalar), self.assertRaisesRegex(AssertionError, "default argument"):
            suite.check_values(object(), True, True)


class MixedUpdateTest(unittest.TestCase):
    """Controlled SQL responses validate the runner, not the database engine."""

    def run_update(self, calls, fail_at=None, stale_default=False, stale_acl=False):
        state = {"updated": False, "reader_grant": False}

        def query(connection, sql, arguments=None):
            calls.append((connection, sql, arguments))
            if len(calls) == fail_at:
                # This also models an unknown UPDATE outcome: no retry/cleanup.
                raise DatabaseError(2013, "injected transport failure")
            if sql.startswith("GRANT EXECUTE"):
                state["reader_grant"] = True
                return ()
            if sql == "ALTER EXTENSION native_math UPDATE TO '1.2'":
                state.update(updated=True, reader_grant=stale_acl)
                return ()
            if sql.startswith(("PREPARE ", "DEALLOCATE PREPARE ")):
                return ()
            if sql.startswith("SELECT extension_id,"):
                return ((8, "1.2"),)
            if sql.startswith("SELECT routine_name,"):
                return replaced_bindings()
            if sql.startswith("SELECT object_id "):
                return tuple((row[1],) for row in replaced_bindings())
            if "FROM oceanbase.__all_plugin_dependency " in sql:
                return ((0 if arguments[0] == "routine.102" else 1,),)
            if "FROM oceanbase.__all_objauth " in sql:
                return ((0,),)
            if sql == "SHOW CREATE FUNCTION native_add_one":
                return (("native_add_one", "", "COMMENT 'native_math 1.2'"),)
            if sql in ("SELECT native_increment()", "EXECUTE native_replace_plan"):
                if connection == "reader" and not state["reader_grant"]:
                    raise DatabaseError(1370, "denied")
                stale_plan = stale_default and sql == "EXECUTE native_replace_plan"
                return ((100 if state["updated"] and not stale_plan else 42,),)
            if sql in ("SELECT native_add_one(%s)", "SELECT native_increment(%s)", "SELECT native_successor(%s)"):
                return ((None if arguments[0] is None else arguments[0] + 1,),)
            raise AssertionError("unhandled test SQL: " + sql)

        with patch.object(suite, "query", side_effect=query):
            return suite.check_mixed_update("admin", "observer", "reader", DRIVER, "fixture", 7, 8,
                                            "'fixture_reader'@'%'", bindings(True))

    def test_mixed_upgrade_checks_prepared_defaults_and_regrants(self):
        calls = []
        self.assertEqual(self.run_update(calls), replaced_bindings())
        self.assertEqual(sum(sql.startswith("ALTER EXTENSION") for _, sql, _ in calls), 1)
        self.assertEqual(sum(sql.startswith("GRANT EXECUTE") for _, sql, _ in calls), 2)
        self.assertEqual(sum(sql.startswith("DEALLOCATE") for _, sql, _ in calls), 2)
        for stale_default, stale_acl in ((True, False), (False, True)):
            with self.subTest(stale_default=stale_default, stale_acl=stale_acl), self.assertRaises(AssertionError):
                self.run_update([], stale_default=stale_default, stale_acl=stale_acl)

    def test_every_transport_failure_stops_without_retry_or_cleanup(self):
        baseline = []
        self.run_update(baseline)
        for position in range(1, len(baseline) + 1):
            calls = []
            with self.subTest(position=position), self.assertRaises((DatabaseError, AssertionError)):
                self.run_update(calls, fail_at=position)
            self.assertEqual(calls, baseline[:position])


class InvokerPackageTest(unittest.TestCase):
    def test_uses_reader_for_ddl_and_checks_owner_and_cleanup(self):
        calls = []

        def query(connection, sql, arguments=None):
            calls.append((connection, sql, arguments))
            if sql.startswith("SELECT user_id"):
                self.assertEqual(arguments, ("fixture_reader", "%"))
                return ((123,),)
            if sql.startswith("SELECT owner_id"):
                return ((123,),)
            if sql.startswith("SELECT seekdb_"):
                self.assertEqual(connection, "reader")
                return ((1,),)
            if sql.startswith("SELECT COUNT(*)"):
                return ((0,),)
            if sql.startswith(("CREATE", "ALTER", "DROP")):
                self.assertEqual(connection, "reader")
            return ()

        with patch.object(suite, "query", side_effect=query):
            suite.check_invoker_package("admin", "reader", 7, "fixture_reader")
        self.assertEqual([sql for _, sql, _ in calls if sql.startswith(("CREATE", "ALTER", "DROP"))],
                         ["CREATE EXTENSION text_ops", "ALTER EXTENSION text_ops UPDATE TO '1.1'",
                          "DROP EXTENSION text_ops"])
        responses = [query(*call) for call in tuple(calls)]
        for position in range(len(responses)):
            with self.subTest(position=position), patch.object(suite, "query") as mocked:
                mocked.side_effect = responses[:position] + [DatabaseError(2013, "lost")]
                with self.assertRaises(DatabaseError):
                    suite.check_invoker_package("admin", "reader", 7, "fixture_reader")
                self.assertEqual(mocked.call_count, position + 1)
        wrong_owner = list(responses)
        wrong_owner[2] = ((999,),)
        with patch.object(suite, "query", side_effect=wrong_owner) as mocked, \
                self.assertRaisesRegex(AssertionError, "owner"):
            suite.check_invoker_package("admin", "reader", 7, "fixture_reader")
        self.assertEqual(mocked.call_count, 3)


class ResourceSafetyTest(unittest.TestCase):
    """Injected responses test runner behavior, not actual SQL behavior."""

    def test_preflight_failure_creates_nothing(self):
        driver = SimpleNamespace(MySQLError=DatabaseError, connect=lambda **_: contextlib.nullcontext(object()))
        options = suite.parse_options(["--port", "2881", "--confirm-disposable-server"])
        output = io.StringIO()
        with patch.object(suite, "scalar", return_value=None), patch.object(suite, "query") as query, \
                contextlib.redirect_stdout(output), self.assertRaisesRegex(AssertionError, "not ready"):
            suite.run(options, driver)
        query.assert_not_called()
        self.assertIn("databases=[], users=[]", output.getvalue())

    def test_failure_retains_only_owned_names_and_never_prints_password(self):
        calls, configs = [], []
        admin = SimpleNamespace(select_db=lambda _: None)

        def connect(**config):
            configs.append(config)
            return contextlib.nullcontext(admin)

        def query(connection, sql, arguments=None):
            calls.append((sql, arguments))
            if arguments is not None:
                # Reproduce PyMySQL's percent interpolation (literal % hazard).
                sql % tuple(repr(value) for value in arguments)
            if sql.startswith("GRANT"):
                raise DatabaseError(2013, "injected")
            return ()

        def scalar(connection, sql, arguments=None):
            return 64 if "SELECT flags" in sql else 42

        driver = SimpleNamespace(MySQLError=DatabaseError, connect=connect)
        options = suite.parse_options(["--unix-socket", "/tmp/sql.sock", "--confirm-disposable-server"])
        output = io.StringIO()
        with patch.object(suite, "scalar", side_effect=scalar), patch.object(suite, "query", side_effect=query), \
                patch.object(suite.secrets, "token_hex", return_value="a" * 20), \
                patch.object(suite.secrets, "token_urlsafe", return_value="secret-not-for-logs"), \
                contextlib.redirect_stdout(output), self.assertRaises(DatabaseError):
            suite.run(options, driver)
        self.assertEqual(configs[0]["unix_socket"], "/tmp/sql.sock")
        self.assertNotIn("host", configs[0])
        self.assertNotIn("port", configs[0])
        self.assertFalse(any(sql.startswith(("DROP", "INSTALL", "UNINSTALL", "SET GLOBAL")) for sql, _ in calls))
        args = next(args for sql, args in calls if sql.startswith("CREATE USER"))
        self.assertLessEqual(len(args[0]), 32)
        self.assertEqual(args[1:], ("%", "secret-not-for-logs"))
        self.assertIn("native_ext_a_" + "a" * 20, output.getvalue())
        self.assertIn("ne_read_" + "a" * 20, output.getvalue())
        self.assertNotIn("secret-not-for-logs", output.getvalue())


if __name__ == "__main__":
    unittest.main()
