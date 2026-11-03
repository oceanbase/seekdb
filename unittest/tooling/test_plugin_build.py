#!/usr/bin/env python3
"""Regression coverage for the plugin Bazel migration (no server required)."""

import importlib.util
import re
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("plugin_tools", ROOT / "bazel/plugin_tools.py")
tools = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tools)


def inventory(path):
    def fail(message):
        raise ValueError(message)
    scope = {"struct": SimpleNamespace, "fail": fail}
    exec(compile((ROOT / path).read_text(), str(path), "exec"), scope)
    return scope


class PluginBuildTest(unittest.TestCase):
    def test_sql_plugin_adapters_do_not_include_host_implementations(self):
        for relative in ("resolver/ddl/extension_routine_resolver.cpp",
                         "engine/expr/caller_catalog_transaction.cpp",
                         "session/ob_sql_session_info.cpp", "executor/ob_cmd_executor.cpp",
                         "engine/cmd/create_extension_executor.cpp",
                         "engine/cmd/alter_extension_executor.cpp"):
            text = (ROOT / "src/sql" / relative).read_text()
            self.assertNotRegex(text, r'#\s*include\s+"(?:observer|rootserver)/', relative)
        resolver = (ROOT / "src/sql/resolver/ddl/extension_routine_resolver.cpp").read_text()
        composition = (ROOT / "src/observer/plugin_catalog_composition.cpp").read_text()
        self.assertNotIn("int CallerRoutineMutation::apply(", resolver)
        self.assertEqual(composition.count("int CallerRoutineMutation::apply("), 1)

    def test_native_routine_schema_header_ownership(self):
        targets = (ROOT / "src/share/share_api_targets.bzl").read_text()
        for name in ("native_routine_signature", "native_routine_create_slot",
                     "native_routine_admission", "routine_schema_overlay",
                     "routine_privilege_overlay", "routine_catalog_savepoint"):
            header = "schema/" + name + ".h"
            self.assertTrue((ROOT / "src/share" / header).is_file())
            self.assertEqual(targets.count('"' + header + '"'), 1, header)

    def test_routine_public_header_closure(self):
        headers = set(inventory("src/rootserver/rootserver_header_inventory.bzl")["ROOTSERVER_PUBLIC_HEADERS"])
        for header in sorted(headers):
            if not header.startswith("pl_ddl/"):
                continue
            text = (ROOT / "src/rootserver" / header).read_text()
            for included in re.findall(r'^\s*#\s*include\s+"rootserver/([^"\n]+)"', text, re.M):
                self.assertIn(included, headers, header + " requires " + included)

    def test_sql_ownership_rejects_invalid_inputs(self):
        data = inventory("src/sql/sql_source_inventory.bzl")
        validate = inventory("src/sql/sql_module_sources.bzl")["validate_plugin_sources"]
        groups = data["SQL_UNITY_GROUPS"] + data["SQL_SIMD_UNITY_GROUPS"]
        standalone = data["SQL_STANDALONE_SOURCES"] + data["SQL_EXTRA_SOURCES"]
        adapters = data["SQL_GIS_PLUGIN_ADAPTER_SOURCES"]
        runtime = data["SQL_EXTENSION_RUNTIME_SOURCES"]
        replaced = data["SQL_CORE_GIS_REPLACED_SOURCES"]
        validate(groups, standalone, adapters, runtime, replaced)
        with self.assertRaisesRegex(ValueError, "duplicate"):
            validate(groups, standalone + [adapters[0]], adapters, runtime, replaced)
        with self.assertRaisesRegex(ValueError, "outside"):
            validate(groups, standalone + ["src/sql/../share/bad.cpp"], adapters, runtime, replaced)
        with self.assertRaisesRegex(ValueError, "distinct baseline"):
            validate(groups, standalone, adapters, runtime, adapters[:1])
        with self.assertRaisesRegex(ValueError, "4 runtime"):
            validate(groups, standalone, adapters, runtime[:-1], replaced)

    def test_generated_schema_outputs_match_bazel(self):
        data = inventory("src/share/share_source_inventory.bzl")
        outputs = inventory("src/share/inner_table_schema.bzl")["inner_table_schema_cpp_outputs"](data["SHARE_UNITY_GROUPS"])
        generator = ROOT / "src/share/inner_table"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run([
                sys.executable, str(generator / "generate_inner_table_schema.py"),
                "--def-file", str(generator / "ob_inner_table_schema_def.py"),
                "--share-output-dir", str(root / "share"),
                "--observer-output-dir", str(root / "observer"), "--quiet",
            ], check=True, capture_output=True, text=True, timeout=30)
            actual = {path.name for path in (root / "share").glob("ob_inner_table_schema.*.cpp")}
            self.assertEqual(actual, {Path(path).name for path in outputs})

    def test_sql_source_selection(self):
        data = inventory("src/sql/sql_source_inventory.bzl")
        helpers = inventory("src/sql/sql_module_sources.bzl")
        groups = helpers["groups_without"](
            data["SQL_UNITY_GROUPS"], helpers["OPTIMIZER_GROUP_NAMES"],
            helpers["PREPARE_SOURCES"] + data["SQL_CORE_GIS_REPLACED_SOURCES"])
        paths = [path for group in groups for path in group.srcs]
        paths += data["SQL_STANDALONE_SOURCES"] + data["SQL_EXTRA_SOURCES"]
        paths += data["SQL_GIS_PLUGIN_ADAPTER_SOURCES"] + data["SQL_EXTENSION_RUNTIME_SOURCES"]
        self.assertEqual(len(paths), len(set(paths)))
        self.assertNotIn("src/sql/engine/expr/ob_geo_expr_utils.cpp", paths)
        for path in paths:
            self.assertTrue((ROOT / path).is_file(), path)
        for name in ("plugin_function_expr", "plugin_sql_context", "caller_catalog_transaction", "index_value_expr"):
            self.assertIn("src/sql/engine/expr/" + name + ".cpp", paths)
        self.assertFalse(any("ob_expr_st_" in path for path in paths))

    def test_package_layout_and_reproducibility(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / "seekdb_gis.so"
            binary.write_bytes(b"test binary")
            manifest = ROOT / "plugins/gis/plugin.toml"
            scripts = list((ROOT / "plugins/gis/sql").glob("*"))
            output = root / "plugin.tar"
            args = SimpleNamespace(name="seekdb_gis_plugin", binary=binary, manifest=manifest, sql=scripts, output=output)
            tools.package(args)
            first = output.read_bytes()
            tools.package(args)
            self.assertEqual(first, output.read_bytes())
            with tarfile.open(output) as archive:
                self.assertIn("lib/seekdb/plugins/seekdb_gis_plugin/plugin.toml", archive.getnames())
                self.assertIn("share/seekdb/extension/gis--1.0.sql", archive.getnames())
                self.assertEqual(archive.getmember("lib/seekdb/plugins/seekdb_gis_plugin/seekdb_gis.so").mode, 0o755)

    def test_sdk_contents(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "sdk.tar"
            headers = list((ROOT / "include/seekdb/plugin").glob("*.h"))
            tools.sdk(SimpleNamespace(output=output, version="1.4.0.0", headers=headers))
            with tarfile.open(output) as archive:
                self.assertEqual(len(archive.getmembers()), len(headers) + 2)
                for header in headers:
                    self.assertEqual(archive.extractfile("include/seekdb/plugin/" + header.name).read(), header.read_bytes())
                config = archive.extractfile("lib/cmake/SeekDBPluginSDK/SeekDBPluginSDKConfig.cmake").read()
                self.assertNotIn(str(ROOT).encode(), config)
                self.assertIn(b"INTERFACE IMPORTED", config)

    def test_archive_rejects_path_escape(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                tools.write_archive(Path(directory) / "bad.tar", {"../escape": b"bad"})

    def test_reject_duplicate_sql_names(self):
        with self.assertRaises(ValueError):
            tools.sql_package(SimpleNamespace(sql=[Path("a/foo.sql"), Path("b/foo.sql")], output=None))

    def test_failed_audit_does_not_publish_binary(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "plugin.so"
            args = SimpleNamespace(manifest=ROOT / "plugins/gis/plugin.toml", profile="public",
                                   exports=[], server_headers=[], binary=Path("unchecked.so"), nm="nm", output=output)
            with mock.patch.object(tools, "check_plugin_includes"), mock.patch.object(tools.subprocess, "run", side_effect=subprocess.CalledProcessError(1, "audit")):
                with self.assertRaises(subprocess.CalledProcessError):
                    tools.audit(args)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
