#!/usr/bin/env python3
"""Run the real legacy deployment script locally, without internal downloads."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]


@unittest.skipUnless(sys.platform == "linux", "requires Linux and Bash")
class ObtestCopyTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="seekdb-obtest-copy-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source with spaces"
        self.build = self.source / "build_release"
        (self.build / "src/observer").mkdir(parents=True)
        (self.build / "lib").mkdir()
        package = self.source / "src/share/inner_table/sys_package"
        package.mkdir(parents=True)
        (package / "fixture.sql").write_text("select 1;\n")
        self.dest = self.root / "deployment with spaces"
        self.dest.mkdir()
        self.script = self.root / "copy.sh"
        self.script.write_bytes((ROOT / "tools/obtest/copy.sh").read_bytes().replace(b"\r\n", b"\n"))
        self.tools = self.root / "tools"
        self.tools.mkdir()
        (self.tools / "libtool").write_text(
            '#!/bin/bash\nshift\n'
            'if [[ -n "${FAIL_COPY:-}" ]]; then exit 17; fi\n'
            'exec "$@"\n')
        (self.tools / "libtool").chmod(0o755)
        # Fail immediately if a test accidentally enters the network path.
        (self.tools / "wget").write_text('#!/bin/bash\nexit 99\n')
        (self.tools / "wget").chmod(0o755)
        self.program("seekdb")

    def program(self, name):
        path = self.build / "src/observer" / name
        path.write_text('#!/bin/sh\necho fixture\n')
        path.chmod(0o755)

    def gpu(self, flag="OB_BUILD_HIPVS", mode="ON"):
        (self.build / "CMakeCache.txt").write_text(f"{flag}:BOOL={mode}\n")
        self.program("seekdb_hipvs_hnsw_worker")
        (self.build / "lib/libseekdb_hipvs_hnsw.so").write_bytes(b"bridge fixture")

    def run_copy(self, **overrides):
        env = dict(os.environ, PATH=str(self.tools) + os.pathsep + os.environ["PATH"])
        for name in ("SEEKDB_BUILD_DIR", "VER", "FAIL_COPY"):
            env.pop(name, None)
        env.update(overrides)
        return subprocess.run(["bash", str(self.script), "-l", str(self.source)],
                              cwd=self.dest, env=env, text=True, capture_output=True, timeout=15)

    def test_gpu_copies_main_worker_and_bridge(self):
        self.gpu()
        # A stale legacy directory must not override the release build.
        (self.source / "build_debug").mkdir()
        result = self.run_copy()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(os.access(self.dest / "bin/observer", os.X_OK))
        self.assertTrue(os.access(self.dest / "bin/seekdb_hipvs_hnsw_worker", os.X_OK))
        self.assertEqual((self.dest / "lib/libseekdb_hipvs_hnsw.so").read_bytes(), b"bridge fixture")

    def test_cuvs_alias_and_explicit_build_directory(self):
        self.gpu("OB_BUILD_CUVS")
        custom = self.source / "custom build"
        self.build.rename(custom)
        result = self.run_copy(SEEKDB_BUILD_DIR=str(custom))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.dest / "bin/seekdb_hipvs_hnsw_worker").exists())

    def test_missing_gpu_bridge_fails_before_main_copy(self):
        self.gpu()
        (self.build / "lib/libseekdb_hipvs_hnsw.so").unlink()
        result = self.run_copy()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Incomplete GPU build", result.stderr)
        self.assertFalse((self.dest / "bin/observer").exists())

    def test_cpu_ignores_stale_gpu_artifacts(self):
        self.gpu(mode="OFF")
        result = self.run_copy()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.dest / "bin/observer").exists())
        self.assertFalse((self.dest / "bin/seekdb_hipvs_hnsw_worker").exists())

    def test_missing_main_fails(self):
        (self.build / "src/observer/seekdb").unlink()
        result = self.run_copy()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Missing executable", result.stderr)

    def test_copy_failure_is_propagated(self):
        self.gpu()
        result = self.run_copy(FAIL_COPY="1")
        self.assertEqual(result.returncode, 17)


if __name__ == "__main__":
    unittest.main()
