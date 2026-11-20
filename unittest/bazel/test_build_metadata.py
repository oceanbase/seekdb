"""Regression tests for the generated C++ version metadata."""

import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("build_metadata", ROOT / "bazel/build_metadata.py")
BUILD_METADATA = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD_METADATA)


class VersionSourceTest(unittest.TestCase):
    def generate(self, directory, branch):
        status = directory / "stable-status.txt"
        status.write_text(
            "STABLE_SEEKDB_GIT_REVISION abcdef123456\n"
            "STABLE_SEEKDB_GIT_BRANCH " + branch + "\n",
            encoding="utf-8",
        )
        output = directory / "ob_version.cpp"
        BUILD_METADATA.version_source(ROOT / "src/share/ob_version.cpp.in", status, output)
        return output.read_text(encoding="utf-8")

    def test_branch_is_inserted_as_literal_metadata(self):
        for branch in ('fix/"quoted"', "fix/@BUILD_FLAGS@", "fix/中文", "master"):
            with self.subTest(branch=branch), tempfile.TemporaryDirectory() as directory:
                source = self.generate(Path(directory), branch)
                expected = branch.replace('"', '\\"')
                self.assertIn('return "' + expected + '";', source)
                self.assertIn('return "1-abcdef123456";', source)
                self.assertIn('return "RelWithDebInfo";', source)

    def test_generated_metadata_compiles_and_preserves_branch(self):
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        if compiler is None:
            self.skipTest("a C++ compiler is required for the generated-source round trip")
        for branch in ('fix/"quoted"', "fix/@BUILD_FLAGS@", "fix/中文", "master"):
            with self.subTest(branch=branch), tempfile.TemporaryDirectory() as directory:
                directory = Path(directory)
                source = self.generate(directory, branch)
                # Compile the actual generated extern-C metadata definitions;
                # the earlier server-version helper needs the full C++ SDK.
                metadata = source[source.index("#ifdef __cplusplus"):]
                translation_unit = directory / "metadata.cpp"
                translation_unit.write_text(
                    '#include <cstdio>\n' + metadata
                    + '\nint main() { std::fputs(build_branch(), stdout); }\n',
                    encoding="utf-8",
                )
                binary = directory / ("metadata.exe" if os.name == "nt" else "metadata")
                subprocess.run(
                    [compiler, "-std=c++17", str(translation_unit), "-o", str(binary)],
                    check=True, capture_output=True,
                )
                result = subprocess.run([str(binary)], check=True, capture_output=True)
                self.assertEqual(branch.encode("utf-8"), result.stdout)


if __name__ == "__main__":
    unittest.main()
