import importlib.util
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "build_metadata", ROOT / "bazel" / "build_metadata.py"
)
build_metadata = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build_metadata)


class VersionSourceTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("c++") or shutil.which("g++"), "C++ compiler required")
    def test_branch_round_trip(self):
        compiler = shutil.which("c++") or shutil.which("g++")
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            status = directory / "stable-status.txt"
            generated = directory / "ob_version.cpp"
            source = directory / "branch.cpp"
            for branch in ("master", 'fix/quoted"branch'):
                with self.subTest(branch=branch):
                    subprocess.run(
                        ["git", "check-ref-format", "refs/heads/" + branch], check=True
                    )
                    status.write_text(
                        "STABLE_SEEKDB_GIT_REVISION abc123\n"
                        "STABLE_SEEKDB_GIT_BRANCH " + branch + "\n",
                        encoding="utf-8",
                    )
                    build_metadata.version_source(
                        ROOT / "src/share/ob_version.cpp.in", status, generated
                    )
                    # Compile the real generated accessor without the database headers.
                    accessor = re.search(
                        r"^.*const char\* build_branch\(\).*$",
                        generated.read_text(encoding="utf-8"),
                        re.MULTILINE,
                    ).group(0)
                    # Check every byte, including the terminator, at compile time.
                    checks = [
                        'static_assert(build_branch()[%d] == %d, "branch mismatch");'
                        % (index, value)
                        for index, value in enumerate(branch.encode("utf-8") + b"\0")
                    ]
                    source.write_text(
                        "constexpr " + accessor.strip() + "\n" + "\n".join(checks),
                        encoding="utf-8",
                    )
                    compiled = subprocess.run(
                        [compiler, "-std=c++17", "-fsyntax-only", str(source)],
                        capture_output=True,
                        text=True,
                    )
                    self.assertEqual(compiled.returncode, 0, compiled.stderr)


if __name__ == "__main__":
    unittest.main()
