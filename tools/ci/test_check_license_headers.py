#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase.
#
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

"""Tests for the covered-source license-header gate."""

from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_license_headers as checker  # noqa: E402


LICENSE_HEADER = """/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the \"License\");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an \"AS IS\" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
"""


class LicenseHeaderTest(unittest.TestCase):
    def test_accepts_standard_header(self):
        self.assertTrue(checker.has_license_header(LICENSE_HEADER.encode("utf-8")))

    def test_accepts_header_after_shebang(self):
        content = (
            "#!/usr/bin/env python3\n"
            "# Copyright (c) 2026 OceanBase.\n"
            "# Licensed under the Apache License, Version 2.0.\n"
        ).encode("utf-8")
        self.assertTrue(checker.has_license_header(content))

    def test_rejects_incomplete_or_late_header(self):
        license_only = b"Licensed under the Apache License, Version 2.0.\n"
        self.assertFalse(checker.has_license_header(license_only))
        late_header = b"\n" * checker.HEADER_MAX_LINES + LICENSE_HEADER.encode(
            "utf-8"
        )
        self.assertFalse(checker.has_license_header(late_header))

    def test_identifies_required_source_paths(self):
        for path in (
            "src/new_file.c",
            "src/new_file.cc",
            "src/new_file.cpp",
            "src/new_file.h",
            "src/new_file.hpp",
            "src/new_file.ipp",
            "src/new_file.rs",
            "src/generated.cpp.in",
        ):
            self.assertTrue(checker.is_required_source_file(path), path)

        for path in (
            ".github/workflows/gate.yml",
            "src/BUILD",
            "config.yml.template",
            "tool",
            "README.md",
            "schema.json",
            "script.sh",
            "script.bash",
            "script.zsh",
            "script.fish",
            "script.pl",
            "script.py",
            "BUILD",
            "BUILD.bazel",
            "BUILD.cpp",
            "CMakeLists.txt",
            "CMakeLists.txt.in",
            "Dockerfile",
            "Dockerfile.cpp",
            "GNUmakefile",
            "Makefile",
            "Makefile.template",
            "WORKSPACE",
            "WORKSPACE.bazel",
            "meson.build",
            "schema.proto",
        ):
            self.assertFalse(checker.is_required_source_file(path), path)


class PullRequestDiffTest(unittest.TestCase):
    def git(self, root, *args):
        result = subprocess.run(
            ["git"] + list(args),
            cwd=str(root),
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        return result.stdout.strip()

    def write(self, root, relative_path, content):
        path = root / relative_path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")

    def test_checks_feature_additions_but_not_renames_or_base_changes(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            self.git(root, "init", "--quiet")
            self.git(root, "config", "user.name", "License Test")
            self.git(root, "config", "user.email", "license-test@example.com")

            self.write(root, "legacy.py", "print('legacy')\n")
            self.write(root, "README.md", "root\n")
            self.git(root, "add", "legacy.py", "README.md")
            self.git(root, "commit", "--quiet", "-m", "root")
            root_commit = self.git(root, "rev-parse", "HEAD")

            self.git(root, "checkout", "--quiet", "-b", "feature")
            self.git(root, "mv", "legacy.py", "renamed.py")
            self.write(root, "good.cpp", LICENSE_HEADER + "int value = 1;\n")
            self.write(root, "missing.cpp", "int missing_header = 1;\n")
            self.write(root, "ignored.py", "print('no header required')\n")
            self.write(root, "tool", "#!/bin/sh\necho 'no header required'\n")
            self.write(root, "notes.md", "documentation is not code\n")
            self.git(
                root,
                "add",
                "good.cpp",
                "missing.cpp",
                "ignored.py",
                "tool",
                "notes.md",
            )
            self.git(root, "commit", "--quiet", "-m", "feature changes")
            head_commit = self.git(root, "rev-parse", "HEAD")

            self.git(root, "checkout", "--quiet", "--detach", root_commit)
            self.write(root, "base_only.py", "print('base only')\n")
            self.git(root, "add", "base_only.py")
            self.git(root, "commit", "--quiet", "-m", "base advances")
            base_commit = self.git(root, "rev-parse", "HEAD")

            repository = checker.GitRepository(root)
            checked, missing = checker.check_added_files(
                repository, base_commit, head_commit
            )

            self.assertEqual(["good.cpp", "missing.cpp"], checked)
            self.assertEqual(["missing.cpp"], missing)

            output = StringIO()
            with redirect_stdout(output):
                return_code = checker.main(
                    [
                        "--repo-root",
                        str(root),
                        "--base-ref",
                        base_commit,
                        "--head-ref",
                        head_commit,
                    ]
                )
            self.assertEqual(1, return_code)
            self.assertIn("missing.cpp", output.getvalue())


if __name__ == "__main__":
    unittest.main()
