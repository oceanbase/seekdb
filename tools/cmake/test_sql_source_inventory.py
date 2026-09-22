#!/usr/bin/env python3
"""Execute the Python-compatible SQL Starlark ownership validator on real inputs.

Tests the checked-in count/path/duplicate checks, not Bazel's evaluator, target
dependencies or compilation. No alternative source inventory is maintained.
"""
import ast
import pathlib
import runpy
import types
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def fail(message):
    raise ValueError(message)


class SqlSourceInventoryTest(unittest.TestCase):
    def setUp(self):
        environment = {"struct": lambda **fields: types.SimpleNamespace(**fields), "fail": fail}
        self.inventory = runpy.run_path(str(ROOT / "src/sql/sql_source_inventory.bzl"), init_globals=environment)
        helpers = runpy.run_path(str(ROOT / "src/sql/sql_module_sources.bzl"), init_globals=environment)
        self.validate = helpers["sql_validate_source_inventory"]
        tree = ast.parse((ROOT / "src/sql/BUILD.bazel").read_text())
        separate, = [ast.literal_eval(node.value) for node in tree.body if isinstance(node, ast.Assign)
                     and any(isinstance(target, ast.Name) and target.id == "_SQL_SEPARATELY_OWNED_SOURCES"
                             for target in node.targets)]
        self.inputs = [self.inventory[name] for name in (
            "SQL_UNITY_GROUPS", "SQL_SIMD_UNITY_GROUPS", "SQL_STANDALONE_SOURCES",
            "SQL_EXTRA_SOURCES", "SQL_PARSER_SOURCES")] + [separate] + [self.inventory[name] for name in (
                "SQL_GIS_PLUGIN_ADAPTER_SOURCES", "SQL_EXTENSION_RUNTIME_SOURCES", "SQL_CORE_GIS_REPLACED_SOURCES")]

    def test_current_inventory(self):
        self.validate(*self.inputs)

    def test_duplicate_is_rejected(self):
        self.inputs[0][0].srcs.append(self.inputs[0][0].srcs[0])
        with self.assertRaisesRegex(ValueError, "duplicate SQL Unity source"):
            self.validate(*self.inputs)

    def test_missing_source_is_rejected(self):
        self.inputs[0][0].srcs.pop()
        with self.assertRaisesRegex(ValueError, "1118 Unity sources"):
            self.validate(*self.inputs)

    def test_unplanned_source_is_rejected(self):
        self.inputs[0][0].srcs.append("src/sql/unplanned.cpp")
        with self.assertRaisesRegex(ValueError, "1118 Unity sources"):
            self.validate(*self.inputs)

    def test_wrong_owner_is_rejected(self):
        self.inputs[0][0].srcs[0] = "src/share/unplanned.cpp"
        with self.assertRaisesRegex(ValueError, "outside src/sql"):
            self.validate(*self.inputs)

    def test_standalone_is_not_duplicated_in_unity(self):
        self.inputs[0][0].srcs[0] = self.inputs[2][0].path
        with self.assertRaisesRegex(ValueError, "duplicate SQL standalone source"):
            self.validate(*self.inputs)

    def test_new_command_and_spi_sources_have_exact_ownership(self):
        unity = [path for group in self.inputs[0] for path in group.srcs]
        standalone = [entry.path for entry in self.inputs[2] + self.inputs[3]]
        for path in ("src/sql/optimizer/log_plugin_custom.cpp",
                     "src/sql/engine/basic/plugin_custom_op.cpp",
                     "src/sql/resolver/cmd/create_extension_resolver.cpp",
                     "src/sql/engine/cmd/create_extension_executor.cpp",
                     "src/sql/resolver/cmd/alter_extension_resolver.cpp",
                     "src/sql/engine/cmd/alter_extension_executor.cpp",
                     "src/sql/resolver/cmd/drop_extension_resolver.cpp",
                     "src/sql/engine/cmd/drop_extension_executor.cpp"):
            self.assertEqual(1, unity.count(path))
            self.assertNotIn(path, standalone)
        for path in ("src/sql/engine/expr/plugin_function_expr.cpp",
                     "src/sql/engine/expr/plugin_sql_context.cpp"):
            self.assertEqual(1, standalone.count(path))
            self.assertNotIn(path, unity)

    def test_conditional_source_cannot_have_a_baseline_owner(self):
        self.inputs[6][0] = self.inputs[0][0].srcs[0]
        with self.assertRaisesRegex(ValueError, "duplicate conditional SQL source"):
            self.validate(*self.inputs)

    def test_conditional_profiles_cannot_share_an_owner(self):
        self.inputs[7][0] = self.inputs[6][0]
        with self.assertRaisesRegex(ValueError, "duplicate conditional SQL source"):
            self.validate(*self.inputs)

    def test_conditional_source_count_is_checked(self):
        self.inputs[7].pop()
        with self.assertRaisesRegex(ValueError, "4 runtime sources"):
            self.validate(*self.inputs)

    def test_stale_gis_exclusion_is_rejected(self):
        self.inputs[8][0] = "src/sql/engine/expr/missing.cpp"
        with self.assertRaisesRegex(ValueError, "distinct baseline owner"):
            self.validate(*self.inputs)

    def test_gis_exclusion_cannot_disable_its_adapter(self):
        self.inputs[8][0] = self.inputs[6][0]
        with self.assertRaisesRegex(ValueError, "distinct baseline owner"):
            self.validate(*self.inputs)


if __name__ == "__main__":
    unittest.main()
