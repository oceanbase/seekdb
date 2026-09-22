#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
"""Metadata admission regression, not compiler/server behavior."""
import copy
import os
import tempfile
import sys
import unittest
from pathlib import Path

from kernel_script import validate_sql_build_configuration

SOURCE = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(SOURCE / "cmake"))
from plugin_boundary_check import validate_plugin_expression_ids


class PluginExpressionIdsTest(unittest.TestCase):
    QUERY = "T_FUN_SYS_PLUGIN_FUNCTION = 1931,\nT_FUN_SYS_PLUGIN_TYPE_ENCODE = 1933,\n"

    def test_actual_query_and_jit_headers(self):
        query = (SOURCE / "src/query/api/query/parser/ob_item_type.h").read_text()
        jit = (SOURCE / "src/objit/include/objit/common/ob_item_type.h").read_text()
        self.assertEqual(validate_plugin_expression_ids(query, jit), [])

    def test_missing_or_changed_copy_is_rejected(self):
        for jit in ["", self.QUERY.replace("1933", "1934")]:
            self.assertTrue(validate_plugin_expression_ids(self.QUERY, jit))

    def test_collision_and_duplicate_name(self):
        for suffix in ["T_FUN_EXISTING = 1933,", "T_FUN_SYS_PLUGIN_TYPE_ENCODE = 1933,"]:
            self.assertTrue(validate_plugin_expression_ids(self.QUERY + suffix, self.QUERY + suffix))

    def test_comments_do_not_declare_functions(self):
        commented = self.QUERY + "/* T_FUN_SYS_PLUGIN_MISSING = 1934, */\n// T_FUN_EXISTING = 1933,"
        self.assertEqual(validate_plugin_expression_ids(self.QUERY, commented), [])

    def test_nonliteral_id_is_rejected(self):
        symbolic = self.QUERY.replace("1933", "SOME_OTHER_ID")
        self.assertTrue(validate_plugin_expression_ids(symbolic, symbolic))


class KernelBuildGateTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="seekdb-build-gate-")
        self.addCleanup(self.temporary.cleanup)
        self.build = Path(self.temporary.name)
        self.directory = self.build / "src/sql"
        self.source = self.directory / "fixture.cpp"
        self.obj = self.directory / "CMakeFiles/ob_sql.dir/fixture.cpp.o"
        self.flags = self.directory / "CMakeFiles/ob_sql.dir/flags.make"
        self.binary = self.build / "src/observer/seekdb"
        self.link = self.build / "src/observer/CMakeFiles/seekdb.dir/link.txt"
        for path, timestamp in [(self.source, 10), (self.flags, 10), (self.obj, 20),
                                (self.link, 10), (self.binary, 30)]:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("controlled metadata fixture\n", encoding="utf-8")
            self.stamp(path, timestamp)
        self.entries = [{"directory": str(self.directory), "file": str(self.source),
                         "command": "c++ -DSEEKDB_WITH_EXPERIMENTAL_PLUGINS=1 -o "
                                    "CMakeFiles/ob_sql.dir/fixture.cpp.o -c fixture.cpp"}]

    @staticmethod
    def stamp(path, value):
        os.utime(path, ns=(value * 1_000_000_000, value * 1_000_000_000))

    def test_noop_configuration_does_not_require_touching_binary(self):
        cmake = self.directory / "CMakeLists.txt"
        cmake.write_text("# newer descriptive input, identical compile/link actions\n", encoding="utf-8")
        self.stamp(cmake, 40)
        validate_sql_build_configuration(self.build, self.entries)

    def test_disabled_sql_executor_is_rejected(self):
        entries = copy.deepcopy(self.entries)
        entries[0]["command"] = entries[0]["command"].replace("-DSEEKDB_WITH_EXPERIMENTAL_PLUGINS=1", "")
        with self.assertRaisesRegex(ValueError, "experimental-plugin definition"):
            validate_sql_build_configuration(self.build, entries)

    def test_flags_or_source_newer_than_object_are_rejected(self):
        for path in [self.flags, self.source]:
            with self.subTest(path=path):
                self.stamp(path, 25)
                with self.assertRaisesRegex(ValueError, "stale"):
                    validate_sql_build_configuration(self.build, self.entries)
                self.stamp(path, 10)

    def test_unlinked_object_is_rejected(self):
        self.stamp(self.obj, 40)
        with self.assertRaisesRegex(ValueError, "stale"):
            validate_sql_build_configuration(self.build, self.entries)

    def test_changed_link_command_is_rejected(self):
        self.stamp(self.link, 40)
        with self.assertRaisesRegex(ValueError, "newer than the executable"):
            validate_sql_build_configuration(self.build, self.entries)

    def test_missing_object_is_rejected(self):
        self.obj.unlink()
        with self.assertRaisesRegex(ValueError, "missing"):
            validate_sql_build_configuration(self.build, self.entries)

    def test_missing_sql_compile_configuration_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "configuration is missing"):
            validate_sql_build_configuration(self.build, [])


if __name__ == "__main__":
    unittest.main()
