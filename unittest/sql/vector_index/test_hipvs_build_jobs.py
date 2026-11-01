#!/usr/bin/env python3
"""Check that memory-constrained GPU builds can cap nested Cargo concurrency."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]


@unittest.skipUnless(sys.platform == "linux" and shutil.which("cmake"),
                     "requires Linux and CMake")
class ExternalCargoJobsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="seekdb-cargo-jobs-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "rust").mkdir()
        shutil.copyfile(ROOT / "rust/rust-toolchain.toml", self.root / "rust/rust-toolchain.toml")
        (self.root / "Cargo.toml").write_text('[package]\nname="test"\nversion="0.0.0"\n')
        (self.root / "Cargo.lock").write_text('version=4\n')
        self.record = self.root / "cargo-arguments.json"
        self.cargo = self.root / "record-cargo"
        self.cargo.write_text(
            f'#!{sys.executable}\nimport json, pathlib, sys\n'
            f'pathlib.Path({str(self.record)!r}).write_text(json.dumps(sys.argv[1:]))\n')
        self.cargo.chmod(0o755)
        (self.root / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.20)\n'
            'project(CargoJobContract LANGUAGES NONE)\n'
            f'set(CARGO "{self.cargo.as_posix()}")\n'
            f'include("{ROOT.as_posix()}/deps/external/cmake/CargoExternal.cmake")\n'
            'seekdb_external_add_cargo_artifacts(\n'
            ' NAME fixture MANIFEST "${CMAKE_SOURCE_DIR}/Cargo.toml"\n'
            ' OUTPUT_ROOT "${CMAKE_BINARY_DIR}/external"\n'
            ' OUTPUTS "${CMAKE_BINARY_DIR}/external/result.a")\n')

    def configure(self, jobs=None, build_name="build"):
        build = self.root / build_name
        command = ["cmake", "-S", str(self.root), "-B", str(build)]
        if jobs is not None:
            command.append("-DSEEKDB_EXTERNAL_CARGO_JOBS=" + jobs)
        result = subprocess.run(command, text=True, capture_output=True, timeout=60)
        return build, result

    def assert_nested_jobs(self, requested, expected):
        build, result = self.configure(requested)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        # Run the real generated CMake launcher. The child records the argv it
        # actually receives, without downloading or building jemalloc.
        result = subprocess.run(["cmake", "-P", str(build / "fixture-cargo.cmake")],
                                text=True, capture_output=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        arguments = json.loads(self.record.read_text())
        self.assertEqual(arguments[arguments.index("--jobs") + 1], expected)

    def test_existing_default_is_preserved(self):
        self.assert_nested_jobs(None, "4")

    def test_single_job_reaches_nested_cargo(self):
        self.assert_nested_jobs("1", "1")

    def test_invalid_job_counts_are_rejected(self):
        for index, value in enumerate(("0", "-1", "many")):
            with self.subTest(value=value):
                _, result = self.configure(value, build_name=f"invalid-{index}")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("positive integer", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
