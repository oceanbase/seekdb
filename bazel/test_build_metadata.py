import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class BuildMetadataTests(unittest.TestCase):

    def check_branch(self, branch):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            status = tmp / "status.txt"
            output = tmp / "version.cpp"
            binary = tmp / "check"

            status.write_text(
                "STABLE_SEEKDB_GIT_REVISION abcdef123456\n"
                f"STABLE_SEEKDB_GIT_BRANCH {branch}\n",
                encoding="utf-8",
            )

            subprocess.run([
                sys.executable,
                str(ROOT / "bazel/build_metadata.py"),
                "version",
                str(ROOT / "src/share/ob_version.cpp.in"),
                str(status),
                str(output),
            ], check=True)

            source = output.read_text(encoding="utf-8")
            line = next(
                x for x in source.splitlines()
                if "build_branch()" in x
            )

            program = (
                "#include <iostream>\n"
                + line
                + "\nint main() { std::cout << build_branch(); }\n"
            )

            subprocess.run([
                "g++", "-std=c++17", "-x", "c++",
                "-", "-o", str(binary),
            ], input=program, text=True, check=True)

            actual = subprocess.check_output(
                [str(binary)], text=True
            )

            self.assertEqual(actual, branch)

    def test_normal_branch(self):
        self.check_branch("fix/normal")

    def test_quoted_branch(self):
        self.check_branch('fix/"quoted"')

    def test_placeholder_branch(self):
        self.check_branch("fix/@BUILD_FLAGS@")

    def test_backslash_branch(self):
        self.check_branch(r"fix/back\slash")


if __name__ == "__main__":
    unittest.main()
