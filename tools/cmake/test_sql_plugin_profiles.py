#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
"""SQL conditional ownership and real CMake selection (configuration, not compilation).

The same data-only inventory and CMake functions are used by production. No
server or Bazel build is performed. Private temporary projects retain no data.
"""
import copy
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from emit_bazel_source_inventory import InventoryError, _read_assignments, emit

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/module_check"))
import sql_source_ownership_check as ownership


class SqlPluginOwnershipTest(unittest.TestCase):
    def setUp(self):
        self.data = _read_assignments(ROOT / "src/sql/sql_source_inventory.bzl")

    def check_modified(self):
        with mock.patch.object(ownership, "_read_assignments", return_value=self.data):
            return ownership.check(ROOT)

    def test_current_workspace_has_exact_ownership(self):
        self.assertEqual(ownership.check(ROOT), [])

    def test_missing_conditional_source_is_not_exempted(self):
        source = self.data["SQL_EXTENSION_RUNTIME_SOURCES"].pop()
        self.assertIn(source, "\n".join(self.check_modified()))

    def test_duplicate_across_profiles_is_rejected(self):
        self.data["SQL_EXTENSION_RUNTIME_SOURCES"][0] = self.data["SQL_GIS_PLUGIN_ADAPTER_SOURCES"][0]
        self.assertIn("duplicate inventory sources", "\n".join(self.check_modified()))

    def test_replacement_reference_does_not_create_an_owner(self):
        self.data["SQL_CORE_GIS_REPLACED_SOURCES"][0] = "src/sql/engine/expr/not_declared.cpp"
        self.assertIn("distinct baseline owners", "\n".join(self.check_modified()))

    def test_profile_source_outside_sql_is_rejected(self):
        self.data["SQL_EXTENSION_RUNTIME_SOURCES"][0] = "src/sql/../share/unplanned.cpp"
        with self.assertRaisesRegex(InventoryError, "invalid SQL source path"):
            self.check_modified()

    def test_data_reader_never_executes_inventory(self):
        with tempfile.TemporaryDirectory(prefix="seekdb-sql-inventory-") as temporary:
            path = Path(temporary) / "malformed.bzl"
            path.write_text("SQL_EXTENSION_RUNTIME_SOURCES = discover_sources()\n", encoding="utf-8")
            with self.assertRaisesRegex(InventoryError, "unsupported inventory expression"):
                _read_assignments(path)

    def test_failed_reference_emission_preserves_previous_configuration(self):
        reader = _read_assignments
        invalid = copy.deepcopy(self.data)
        invalid["SQL_CORE_GIS_REPLACED_SOURCES"][0] = invalid["SQL_EXTENSION_RUNTIME_SOURCES"][0]
        def modified(path):
            return invalid if path == ROOT / "src/sql/sql_source_inventory.bzl" else reader(path)
        with tempfile.TemporaryDirectory(prefix="seekdb-sql-emission-") as temporary:
            output = Path(temporary) / "inventory.cmake"
            output.write_text("previous valid inventory\n", encoding="utf-8")
            with mock.patch("emit_bazel_source_inventory._read_assignments", side_effect=modified):
                with self.assertRaisesRegex(InventoryError, "distinct baseline SQL owners"):
                    emit(ROOT, output)
            self.assertEqual(output.read_text(), "previous valid inventory\n")

    def test_all_four_cmake_profiles(self):
        data = self.data
        baseline = [path for group in data["SQL_UNITY_GROUPS"] for path in group["srcs"]]
        baseline += [record["path"] for record in data["SQL_STANDALONE_SOURCES"]]
        replacements = set(data["SQL_CORE_GIS_REPLACED_SOURCES"])
        adapters = set(data["SQL_GIS_PLUGIN_ADAPTER_SOURCES"])
        runtime = set(data["SQL_EXTENSION_RUNTIME_SOURCES"])
        with tempfile.TemporaryDirectory(prefix="seekdb-sql-profiles-") as temporary:
            stage = Path(temporary)
            # The generated inventory is source-root-relative. A read-only view
            # of real sources lets CMake generate targets without copying them.
            (stage / "src").symlink_to(ROOT / "src", target_is_directory=True)
            emit(ROOT, stage / "inventory.cmake")
            (stage / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.20)
project(sql_profile LANGUAGES CXX)
include("''' + (ROOT / "cmake/Utils.cmake").as_posix() + '''")
include("${CMAKE_SOURCE_DIR}/inventory.cmake")
seekdb_apply_unity_inventory(ob_sql SEEKDB_SQL_UNITY)
seekdb_apply_standalone_inventory(ob_sql SEEKDB_SQL_STANDALONE)
add_library(ob_sql OBJECT ${ob_sql_cache_objects_})
seekdb_apply_sql_plugin_profile(ob_sql)
get_target_property(selected ob_sql SOURCES)
get_target_property(definitions ob_sql COMPILE_DEFINITIONS)
if(NOT definitions)
  set(definitions "")
endif()
file(WRITE "${CMAKE_BINARY_DIR}/selected.txt" "${selected}")
file(WRITE "${CMAKE_BINARY_DIR}/definitions.txt" "${definitions}")
# Exact paths, not basenames: an unrelated namesake must never be excluded.
set(namesake "${CMAKE_SOURCE_DIR}/unrelated/ob_expr_st_transform.cpp")
seekdb_filter_core_gis_sql_sources(kept "${namesake}")
if(NOT kept STREQUAL namesake)
  message(FATAL_ERROR "GIS profile removed an unrelated same-named source")
endif()
''', encoding="utf-8")
            for core_gis in (False, True):
                for experimental in (False, True):
                    with self.subTest(core_gis=core_gis, experimental=experimental):
                        build = stage / f"build-{int(core_gis)}-{int(experimental)}"
                        result = subprocess.run([
                            "cmake", "-S", str(stage), "-B", str(build),
                            "-DSEEKDB_ENABLE_CORE_GIS=" + ("ON" if core_gis else "OFF"),
                            "-DSEEKDB_ENABLE_EXPERIMENTAL_PLUGINS=" + ("ON" if experimental else "OFF"),
                        ], capture_output=True, text=True, timeout=45)
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        selected = [Path(path).relative_to(stage).as_posix()
                                    for path in (build / "selected.txt").read_text().split(";")]
                        self.assertEqual(len(selected), len(set(selected)), "duplicate compilation owners")
                        expected = set(baseline)
                        if not core_gis:
                            expected = (expected - replacements) | adapters
                        if experimental:
                            expected |= runtime
                        self.assertEqual(set(selected), expected)
                        definitions = (build / "definitions.txt").read_text().split(";")
                        self.assertEqual("SEEKDB_WITH_EXPERIMENTAL_PLUGINS=1" in definitions, experimental)


if __name__ == "__main__":
    unittest.main()
