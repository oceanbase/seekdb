#!/usr/bin/env python3

import tempfile
import textwrap
import unittest
from pathlib import Path

import unittest_module_check as checker


POLICY_TEXT = r'''
MODULE_ROOTS = {
    "oblib": "src/oblib",
    "observer": "src/observer",
    "share": "src/share",
    "sql": "src/sql",
    "storage": "src/storage",
}

UNITTEST_MODULE_ROOTS = {
    "oblib": "unittest/oblib",
    "share": "unittest/share",
    "sql": "unittest/sql",
    "storage": "unittest/storage",
}

UNITTEST_ALLOWED_DIRECT_MODULE_DEPS = {
    "oblib": [],
    "share": ["oblib"],
    "sql": ["oblib"],
    "storage": ["oblib"],
}

UNITTEST_RUNTIME_DEPS = ["//src/observer:liboceanbase"]
UNITTEST_INFRASTRUCTURE_FILES = [
    "unittest/BUILD.bazel",
    "unittest/all_tests_main.cpp",
]
UNITTEST_FORBIDDEN_PATH_PATTERNS = [
    "(^|/)(benchmark|benchmarks)(/|$)",
    "(^|/)[^/]*(perf|performance|stress|pressure)[^/]*",
]
UNITTEST_FORBIDDEN_CASE_PATTERN = "(benchmark|stress|performance|performace|(^|_)perf($|_)|large_thread|max_concurrent_task)"
'''


class UnitTestModuleCheckTest(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        policy_path = self.root / "bazel/architecture/module_policy.bzl"
        policy_path.parent.mkdir(parents=True)
        policy_path.write_text(POLICY_TEXT, encoding="utf-8")
        self.policy = checker.load_policy(policy_path)

    def tearDown(self):
        self.temporary_directory.cleanup()

    def write(self, relative, contents):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(textwrap.dedent(contents), encoding="utf-8")

    def violations(self):
        return checker.audit_repository(self.root, self.policy).violations

    def test_own_module_and_foundation_includes_are_allowed(self):
        self.write(
            "unittest/sql/test_ok.cpp",
            """
            #include "sql/engine/ob_operator.h"
            #include "lib/container/ob_array.h"
            """,
        )
        self.assertEqual((), self.violations())

    def test_easy_is_owned_by_oblib(self):
        self.write(
            "unittest/oblib/test_easy.cpp",
            '#include "src/oblib/easy/io/easy_io.h"\n',
        )
        self.write(
            "unittest/oblib/BUILD.bazel",
            """
            cc_test(
                name = "test_easy",
                srcs = ["test_easy.cpp"],
                deps = ["//src/oblib/easy:easy"],
            )
            """,
        )
        self.assertEqual((), self.violations())

    def test_peer_module_include_is_rejected(self):
        self.write(
            "unittest/sql/test_bad.cpp",
            '#include "storage/tablet/ob_tablet.h"\n',
        )
        self.assertEqual(
            [
                (
                    "cross_module_include",
                    "unittest/sql/test_bad.cpp",
                    "sql",
                    "storage",
                )
            ],
            [
                (item.kind, item.path, item.owner, item.dependency)
                for item in self.violations()
            ],
        )

    def test_commented_and_literal_if_zero_includes_are_ignored(self):
        self.write(
            "unittest/sql/test_ok.cpp",
            """
            // #include "storage/ignored.h"
            /*
            #include "storage/also_ignored.h"
            */
            #if 0
            #include "storage/disabled.h"
            #endif
            #include "sql/enabled.h"
            """,
        )
        self.assertEqual((), self.violations())

    def test_peer_build_dependency_is_rejected_but_runtime_is_allowed(self):
        self.write("unittest/sql/test_ok.cpp", '#include "sql/ok.h"\n')
        self.write(
            "unittest/sql/BUILD.bazel",
            """
            cc_test(
                name = "test_ok",
                srcs = ["test_ok.cpp"],
                deps = [
                    "//src/observer:liboceanbase",
                    "//src/sql:sql_test_interface",
                    "//src/storage:storage_test_interface",
                ],
            )
            """,
        )
        self.assertEqual(
            [
                (
                    "cross_module_dep",
                    "unittest/sql/BUILD.bazel",
                    "sql",
                    "storage",
                )
            ],
            [
                (item.kind, item.path, item.owner, item.dependency)
                for item in self.violations()
            ],
        )

    def test_unowned_and_performance_programs_are_rejected(self):
        self.write("unittest/orphan/helper.h", "\n")
        self.write("unittest/sql/test_parser_perf.cpp", "\n")
        self.assertEqual(
            {"non_unit_program", "unowned"},
            {item.kind for item in self.violations()},
        )

    def test_performance_case_inside_correctness_file_is_rejected(self):
        self.write(
            "unittest/sql/test_mixed.cpp",
            """
            TEST(SqlUtils, correctness) {}
            TEST(SqlUtils, mem_perf) {}
            TEST(SqlUtils, perfect_forwarding) {}
            """,
        )
        self.assertEqual(
            [("non_unit_case", "mem_perf")],
            [
                (item.kind, item.detail)
                for item in self.violations()
            ],
        )

    def test_commented_performance_case_is_ignored(self):
        self.write(
            "unittest/sql/test_mixed.cpp",
            """
            // TEST(SqlUtils, line_comment_perf) {}
            /*
            TEST(SqlUtils, block_comment_stress) {}
            */
            TEST(SqlUtils, correctness) {}
            """,
        )
        self.assertEqual((), self.violations())

    def test_standalone_main_without_gtest_case_is_rejected(self):
        self.write(
            "unittest/sql/manual_driver.cpp",
            """
            int main(int argc, char **argv) { return argc == 0; }
            """,
        )
        self.assertEqual(
            [(
                "non_unit_executable",
                "module tests must use unittest/all_tests_main.cpp",
            )],
            [
                (item.kind, item.detail)
                for item in self.violations()
            ],
        )

    def test_legacy_gtest_main_is_rejected(self):
        self.write(
            "unittest/sql/legacy_test.cpp",
            """
            TEST(LegacyTest, basic) {}
            int main(int argc, char **argv) { return RUN_ALL_TESTS(); }
            """,
        )
        self.assertEqual(
            [(
                "non_unit_executable",
                "module tests must use unittest/all_tests_main.cpp",
            )],
            [
                (item.kind, item.detail)
                for item in self.violations()
            ],
        )

    def test_repository_test_main_is_infrastructure(self):
        self.write("unittest/all_tests_main.cpp", "\n")
        self.assertEqual((), self.violations())

    def test_repository_build_file_is_infrastructure(self):
        self.write("unittest/BUILD.bazel", 'exports_files(["all_tests_main.cpp"])\n')
        self.assertEqual((), self.violations())


if __name__ == "__main__":
    unittest.main()
