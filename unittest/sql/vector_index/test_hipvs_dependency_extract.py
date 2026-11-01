"""Exercise fallback extraction fidelity and containment using real archives."""
import ctypes.util
import importlib.util
import io
import os
from pathlib import Path
import stat
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("extract_rpm", ROOT / "deps/init/extract_rpm.py")
EXTRACTOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXTRACTOR)


@unittest.skipUnless(ctypes.util.find_library("archive"), "system libarchive is unavailable")
class DependencyExtractionTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="seekdb-rpm-extraction-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.archive = self.root / "payload.tar.gz"
        self.destination = self.root / "private dependencies"

    def write_archive(self, entries):
        with tarfile.open(self.archive, "w:gz") as output:
            for entry, content in entries:
                output.addfile(entry, io.BytesIO(content) if content else None)

    def regular(self, name, content=b"dependency contents", mode=0o755):
        entry = tarfile.TarInfo(name)
        entry.size = len(content)
        entry.mode = mode
        return entry, content

    def test_file_modes_symlink_and_hardlink_are_preserved(self):
        symbolic = tarfile.TarInfo("usr/bin/tool-link")
        symbolic.type = tarfile.SYMTYPE
        symbolic.linkname = "tool"
        hard = tarfile.TarInfo("usr/bin/tool-hardlink")
        hard.type = tarfile.LNKTYPE
        hard.linkname = "usr/bin/tool"
        self.write_archive([self.regular("usr/bin/tool"), (symbolic, b""), (hard, b"")])
        EXTRACTOR.extract(self.archive, self.destination)
        tool = self.destination / "usr/bin/tool"
        self.assertEqual(tool.read_bytes(), b"dependency contents")
        self.assertEqual(stat.S_IMODE(tool.stat().st_mode), 0o755)
        self.assertEqual(os.readlink(self.destination / "usr/bin/tool-link"), "tool")
        self.assertEqual(tool.stat().st_ino, (self.destination / "usr/bin/tool-hardlink").stat().st_ino)

    def test_parent_path_does_not_escape_destination(self):
        self.write_archive([self.regular("../escaped")])
        with self.assertRaises(RuntimeError):
            EXTRACTOR.extract(self.archive, self.destination)
        self.assertFalse((self.root / "escaped").exists())

    def test_absolute_path_is_rejected(self):
        outside = self.root / "absolute-escaped"
        self.write_archive([self.regular(str(outside))])
        with self.assertRaises(RuntimeError):
            EXTRACTOR.extract(self.archive, self.destination)
        self.assertFalse(outside.exists())

    def test_archive_cannot_write_through_external_symlink(self):
        outside = self.root / "outside"
        outside.mkdir()
        symbolic = tarfile.TarInfo("redirect")
        symbolic.type = tarfile.SYMTYPE
        symbolic.linkname = str(outside)
        self.write_archive([(symbolic, b""), self.regular("redirect/escaped")])
        with self.assertRaises(RuntimeError):
            EXTRACTOR.extract(self.archive, self.destination)
        self.assertFalse((outside / "escaped").exists())

    def test_invalid_archive_fails(self):
        self.archive.write_bytes(b"not an RPM or supported archive")
        with self.assertRaises(RuntimeError):
            EXTRACTOR.extract(self.archive, self.destination)


if __name__ == "__main__":
    unittest.main()
