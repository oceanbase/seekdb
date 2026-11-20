"""Regression tests for the Bazel build metadata generator.

These tests guard against the bugs reported in oceanbase/seekdb#1578:

1. A Git branch name containing double quotes (legal per ``git check-ref-format``)
   used to be interpolated verbatim into a C++ string literal, producing invalid
   C++ such as ``return "fix/"quoted"";``.
2. A branch name containing placeholder-like text (e.g. ``fix/@BUILD_FLAGS@``)
   used to be substituted a second time, corrupting the stored value.
"""

import importlib.util
from pathlib import Path
import tempfile
import unittest

REPO_ROOT = Path(__file__).resolve().parents[3]
SCRIPT = REPO_ROOT / "bazel" / "build_metadata.py"
SPEC = importlib.util.spec_from_file_location("seekdb_build_metadata", SCRIPT)
build_metadata = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build_metadata)

TEMPLATE = REPO_ROOT / "src" / "share" / "ob_version.cpp.in"


class BuildMetadataTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.status_file = self.root / "stable-status.txt"
        self.output = self.root / "ob_version.cpp"

    def run_generator(self, branch, revision="abcdef123456"):
        self.status_file.write_text(
            "STABLE_SEEKDB_GIT_REVISION {}\n"
            "STABLE_SEEKDB_GIT_BRANCH {}\n".format(revision, branch),
            encoding="utf-8",
        )
        build_metadata.version_source(TEMPLATE, self.status_file, self.output)
        return self.output.read_text(encoding="utf-8")

    def build_branch(self, source):
        for line in source.splitlines():
            if "build_branch()" in line:
                return line
        self.fail("build_branch() not found in generated source")

    def test_normal_branch_unchanged(self):
        source = self.run_generator("master")
        self.assertIn(
            'const char* build_branch() { return "master"; }',
            self.build_branch(source),
        )
        self.assertIn('build_version() { return "1-abcdef123456"; }', source)
        self.assertIn('build_flags() { return "RelWithDebInfo"; }', source)

    def test_quoted_branch_is_escaped(self):
        source = self.run_generator('fix/"quoted"')
        # The embedded double quotes must be escaped so the literal is valid C++.
        self.assertIn(
            'const char* build_branch() { return "fix/\\"quoted\\""; }',
            self.build_branch(source),
        )

    def test_backslash_branch_is_escaped(self):
        source = self.run_generator("fix/back\\slash")
        self.assertIn(
            'const char* build_branch() { return "fix/back\\\\slash"; }',
            self.build_branch(source),
        )

    def test_placeholder_text_is_literal(self):
        source = self.run_generator("fix/@BUILD_FLAGS@")
        # The placeholder-like text must be preserved, not substituted again.
        self.assertIn(
            'const char* build_branch() { return "fix/@BUILD_FLAGS@"; }',
            self.build_branch(source),
        )
        # build_flags itself must still resolve to the real build flag value.
        self.assertIn('build_flags() { return "RelWithDebInfo"; }', source)


if __name__ == "__main__":
    unittest.main()
