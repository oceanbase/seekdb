import hashlib
import importlib.util
import io
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import unittest
from unittest import mock


spec = importlib.util.spec_from_file_location(
    "seekdb_build_launcher", Path(__file__).resolve().parents[1] / "build.py"
)
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)


@unittest.skipIf(os.name == "nt", "POSIX executable permissions are required")
class CachedBazelTests(unittest.TestCase):
    payload = b"#!/bin/sh\nprintf 'cached-bazel\\n'\n"

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.binary = Path(self.directory.name) / "bazel"
        self.checksum = hashlib.sha256(self.payload).hexdigest()

    def check_cached_binary(self, mode, expected_mode):
        self.binary.write_bytes(self.payload)
        self.binary.chmod(mode)
        with mock.patch.object(launcher.urllib.request, "build_opener") as opener:
            result = launcher._download_binary(
                "https://example.invalid/bazel", self.binary, self.checksum
            )
            opener.assert_not_called()
        self.assertEqual(result, self.binary)
        self.assertEqual(stat.S_IMODE(result.stat().st_mode), expected_mode)
        self.assertEqual(result.read_bytes(), self.payload)
        self.assertEqual(subprocess.check_output([str(result)]), b"cached-bazel\n")

    def test_cached_binary_without_execute_permission_is_repaired(self):
        self.check_cached_binary(0o640, 0o740)

    def test_executable_cache_retains_existing_permissions(self):
        self.check_cached_binary(0o750, 0o750)

    def test_corrupt_cache_is_downloaded_and_verified(self):
        self.binary.write_bytes(b"corrupt cache")
        self.binary.chmod(0o640)
        opener = mock.Mock()
        opener.open.return_value = io.BytesIO(self.payload)
        with mock.patch.object(launcher.urllib.request, "build_opener", return_value=opener):
            result = launcher._download_binary(
                "https://example.invalid/bazel", self.binary, self.checksum
            )
        opener.open.assert_called_once()
        self.assertEqual(result.read_bytes(), self.payload)
        self.assertEqual(subprocess.check_output([str(result)]), b"cached-bazel\n")


if __name__ == "__main__":
    unittest.main()
