"""Regression tests for Bazel build metadata generation."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "bazel" / "build_metadata.py"


class BuildMetadataTests(unittest.TestCase):
    def test_version_source_with_regular_status(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            template = root / "version.in"
            template.write_text("@GIT_REVISION@|@GIT_BRANCH@|@BUILD_NUMBER@\n", encoding="utf-8")
            status = root / "stable-status.txt"
            status.write_text(
                "STABLE_SEEKDB_GIT_REVISION deadbeef\n"
                "STABLE_SEEKDB_GIT_BRANCH feature/demo\n",
                encoding="utf-8",
            )
            output = root / "version.cpp"

            result = subprocess.run(
                [sys.executable, str(SCRIPT), "version", str(template), str(status), str(output)],
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(output.read_text(encoding="utf-8"), "deadbeef|feature/demo|1\n")

    def test_version_source_ignores_blank_status_lines(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            template = root / "version.in"
            template.write_text("@GIT_REVISION@|@GIT_BRANCH@\n", encoding="utf-8")
            status = root / "stable-status.txt"
            status.write_text(
                "\nSTABLE_SEEKDB_GIT_REVISION deadbeef\n"
                "\nSTABLE_SEEKDB_GIT_BRANCH feature/demo\n\n",
                encoding="utf-8",
            )
            output = root / "version.cpp"

            result = subprocess.run(
                [sys.executable, str(SCRIPT), "version", str(template), str(status), str(output)],
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(output.read_text(encoding="utf-8"), "deadbeef|feature/demo\n")

    def test_workspace_status_without_git_binary(self):
        with tempfile.TemporaryDirectory() as directory:
            environment = os.environ.copy()
            environment["BUILD_WORKSPACE_DIRECTORY"] = directory
            environment["PATH"] = ""

            result = subprocess.run(
                [sys.executable, str(SCRIPT), "status"],
                capture_output=True,
                text=True,
                env=environment,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                result.stdout,
                "STABLE_SEEKDB_GIT_REVISION unknown\n"
                "STABLE_SEEKDB_GIT_BRANCH unknown\n",
            )


if __name__ == "__main__":
    unittest.main()
