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
"""Offline GIS runner safeguards; not database/catalog correctness evidence."""
from collections import Counter
import contextlib
import io
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import gis_extension_server as suite
from test_query_catalog_server import Connection, Cursor, DatabaseError


def records(base=1000):
    rows, slots = [], Counter()
    for (name, implementation), count in suite.expected_bindings().items():
        for _ in range(count):
            rows.append((name, base + len(rows), suite.MODULE, implementation, 1, slots[name]))
            slots[name] += 1
    return rows


class HelpersTest(unittest.TestCase):
    def test_package_inventory_and_overloads(self):
        expected = suite.expected_bindings()
        self.assertEqual(sum(expected.values()), 106)
        self.assertEqual(len({name for name, _ in expected}), 82)
        suite.check_bindings(records(), expected)
        for index, value in ((0, "wrong"), (1, None), (1, True), (1, -1), (1, 1 << 63),
                             (1, 1001), (2, "other.module"), (3, "org.seekdb.gis.function.alias.point"),
                             (4, True), (4, 2), (5, True), (5, -1), (5, 8)):
            damaged = records()
            changed = list(damaged[0]); changed[index] = value; damaged[0] = tuple(changed)
            with self.subTest(index=index, value=value), self.assertRaises(AssertionError):
                suite.check_bindings(damaged, expected)
        for damaged in (records()[:-1], records() + [records()[0]]):
            with self.assertRaises(AssertionError):
                suite.check_bindings(damaged, expected)
        damaged = records()
        # Keep all names/counts/identities but collide two slots within a family.
        indices = [i for i, row in enumerate(damaged) if row[0] == "st_makepoint"]
        changed = list(damaged[indices[1]]); changed[5] = 0; damaged[indices[1]] = tuple(changed)
        with self.assertRaisesRegex(AssertionError, "placement"):
            suite.check_bindings(damaged, expected)

    def test_catalog_queries_bind_ids_and_names(self):
        with patch.object(suite, "query", return_value=()) as query:
            suite.routines(object(), 42)
            self.assertEqual(query.call_args.args[2], (42,))
            suite.installation(object(), 43)
            self.assertEqual(query.call_args.args[2], (43,))
        rows = records()[:2]
        with patch.object(suite, "query", return_value=((1000,), (1001,))):
            suite.check_members(object(), 42, 7, rows)
        for members in ((), ((1000,), (1000,)), ((1000,), (1999,))):
            with patch.object(suite, "query", return_value=members), self.assertRaises(AssertionError):
                suite.check_members(object(), 42, 7, rows)
        for present in (True, False):
            with patch.object(suite, "scalar", return_value=int(present)) as scalar:
                suite.check_edges(object(), rows, present)
                self.assertEqual(scalar.call_args_list[0].args[2],
                                 ("routine.1000", suite.MODULE, rows[0][3]))
            with patch.object(suite, "scalar", return_value=2), self.assertRaises(AssertionError):
                suite.check_edges(object(), rows, present)

    def test_error_or_null_is_not_a_successful_value(self):
        for wrong in (None, 0, 2):
            with patch.object(suite, "scalar", return_value=wrong), self.assertRaises(AssertionError):
                suite.check_values(object())
        with patch.object(suite, "scalar", side_effect=DatabaseError(2013, "lost")), \
                self.assertRaises(DatabaseError):
            suite.check_values(object())
        with patch.object(suite, "scalar", return_value=1) as calls:
            suite.check_values(object())
            self.assertEqual(calls.call_count, 13)

    def test_removal_checks_live_acl_not_history_or_only_routines(self):
        with patch.object(suite, "installation", return_value=()), patch.object(suite, "routines", return_value=()), \
                patch.object(suite, "check_members"), patch.object(suite, "check_edges"), \
                patch.object(suite, "scalar", return_value=0) as scalar:
            suite.check_removed(object(), 42, 7, records())
            self.assertEqual(scalar.call_args.args[2], tuple(row[1] for row in records()))
            self.assertIn("objtype=9", scalar.call_args.args[1])
            self.assertNotIn("history", scalar.call_args.args[1])
        for count in (None, 1, 106):
            with patch.object(suite, "installation", return_value=()), patch.object(suite, "routines", return_value=()), \
                    patch.object(suite, "check_members"), patch.object(suite, "check_edges"), \
                    patch.object(suite, "scalar", return_value=count), self.assertRaisesRegex(AssertionError, "live object ACL"):
                suite.check_removed(object(), 42, 7, records())

    def test_internal_and_transport_errors_cannot_count_as_rejection(self):
        driver = SimpleNamespace(MySQLError=DatabaseError)
        for code in (4016, 4109, 1146, 2003, 2006, 2013):
            for allowed in (suite.MISSING, suite.DENIED, {4179}):
                with self.subTest(code=code, allowed=allowed), self.assertRaises(AssertionError):
                    suite.reject(Connection(Cursor(error=DatabaseError(code, "error"))), "SQL", driver, allowed)


class RunnerSafetyTest(unittest.TestCase):
    def options(self):
        return suite.parse_options(["--port", "2881", "--confirm-disposable-server"], description=suite.__doc__)

    def test_requires_confirmation_even_when_called_as_a_module(self):
        options = self.options(); options.confirm_disposable_server = False
        driver = SimpleNamespace(connect=lambda **_: self.fail("must not connect"))
        with self.assertRaisesRegex(AssertionError, "confirmation"):
            suite.run(options, driver)

    def test_read_only_preflight_failure_creates_nothing(self):
        driver = SimpleNamespace(connect=lambda **_: contextlib.nullcontext(object()))
        output = io.StringIO()
        with patch.object(suite, "query", return_value=()) as query, \
                patch.object(suite, "scalar", return_value=0), contextlib.redirect_stdout(output), \
                self.assertRaisesRegex(AssertionError, "implementation-only"):
            suite.run(self.options(), driver)
        self.assertEqual(query.call_count, 1)
        self.assertTrue(query.call_args.args[1].startswith("SELECT"))
        self.assertIn("attempted creations (inspect unknown outcomes)=[]", output.getvalue())

    def run_model(self, fail_prefix=None, options=None):
        """Only SQL sequencing/resource ownership is modeled, not a server."""
        calls, configs, rejections = [], [], []
        output = io.StringIO()
        suffix = "a" * 20
        def connect(**config):
            configs.append(config)
            return contextlib.nullcontext(Connection())
        def query(connection, sql, arguments=None):
            calls.append((sql, arguments))
            if arguments is not None:
                sql % tuple(repr(value) for value in arguments)  # PyMySQL percent hazard.
            if fail_prefix and sql.startswith(fail_prefix):
                raise DatabaseError(2013, "injected unknown outcome")
            return ()
        def scalar(connection, sql, arguments=None):
            if "SELECT flags" in sql: return 64
            if "SELECT database_id" in sql: return 42 if "_a_" in arguments[0] else 43
            if sql == "EXECUTE gis_plan": return 5
            if ".ST_X(" in sql: return 3
            return b"geometry"
        with contextlib.ExitStack() as stack:
            for target, kwargs in (
                ("query", {"side_effect": query}), ("scalar", {"side_effect": scalar}),
                ("reject", {"side_effect": lambda c, sql, d, codes: rejections.append((sql, codes))}),
                ("check_values", {}), ("check_removed", {}), ("check_edges", {}),
                ("installation", {"return_value": ()}), ("routines", {"return_value": ()}),
                ("check_installation", {"side_effect": [(7, records()), (8, records(2000)), (9, records(3000))]}),
            ):
                stack.enter_context(patch.object(suite, target, **kwargs))
            stack.enter_context(patch.object(suite.secrets, "token_hex", return_value=suffix))
            stack.enter_context(patch.object(suite.secrets, "token_urlsafe", return_value="private-fixture-password"))
            stack.enter_context(contextlib.redirect_stdout(output))
            driver = SimpleNamespace(connect=connect, MySQLError=DatabaseError)
            if fail_prefix:
                with self.assertRaises(DatabaseError): suite.run(options or self.options(), driver)
            else:
                suite.run(options or self.options(), driver)
        return calls, configs, rejections, output.getvalue()

    def test_success_model_covers_scope_and_typed_acl_then_cleans_only_owned_names(self):
        calls, configs, rejections, output = self.run_model()
        statements = [sql for sql, _ in calls]
        self.assertEqual(statements.count("CREATE EXTENSION gis"), 3)
        self.assertEqual(statements.count("DROP EXTENSION gis"), 2)
        self.assertEqual(sum(sql.startswith("DROP DATABASE") for sql in statements), 2)
        self.assertEqual(sum(sql.startswith("DROP USER") for sql in statements), 1)
        self.assertTrue(any("ST_MakePoint(DOUBLE,DOUBLE)" in sql and sql.startswith("GRANT") for sql in statements))
        self.assertIn(("SELECT ST_MakePoint(3,4,5)", suite.DENIED), rejections)
        self.assertIn(("EXECUTE gis_acl_plan", suite.DENIED), rejections)
        self.assertIn(("EXECUTE gis_plan", suite.MISSING | {1615}), rejections)
        self.assertTrue(all(c["autocommit"] and c["host"] == "127.0.0.1" for c in configs))
        self.assertNotIn("private-fixture-password", output)
        self.assertIn("PASS:", output)

    def test_unix_socket_never_falls_back_to_tcp(self):
        options = suite.parse_options(["--unix-socket", "/tmp/gis-test.sock", "--confirm-disposable-server"])
        _, configs, _, _ = self.run_model(options=options)
        self.assertTrue(configs)
        for config in configs:
            self.assertEqual(config["unix_socket"], "/tmp/gis-test.sock")
            self.assertNotIn("host", config)
            self.assertNotIn("port", config)

    def test_failure_retains_known_and_unknown_outcomes_without_cleanup_or_replay(self):
        for prefix in ("CREATE DATABASE", "CREATE USER", "GRANT CREATE ROUTINE", "CREATE EXTENSION", "DROP EXTENSION"):
            with self.subTest(prefix=prefix):
                calls, _, _, output = self.run_model(prefix)
                self.assertTrue(calls[-1][0].startswith(prefix))
                self.assertEqual(sum(sql.startswith(prefix) for sql, _ in calls), 1)
                self.assertFalse(any(sql.startswith(("DROP DATABASE", "DROP USER", "INSTALL", "UNINSTALL", "SET GLOBAL"))
                                     for sql, _ in calls))
                self.assertIn("gis_ext_a_" + "a" * 20, output)
                self.assertIn("attempted creations", output)
                self.assertNotIn("private-fixture-password", output)
                self.assertNotIn("PASS:", output)


if __name__ == "__main__":
    unittest.main()
