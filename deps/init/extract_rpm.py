#!/usr/bin/env python3
"""Extract dependency RPMs with system libarchive when rpm2cpio is unavailable."""
import argparse
import ctypes
import ctypes.util
import os
from pathlib import Path, PurePosixPath
import stat
import sys


def load_archive():
    library = ctypes.util.find_library("archive")
    if not library:
        raise RuntimeError("system libarchive is unavailable")
    archive = ctypes.CDLL(library)
    signatures = {
        "archive_read_new": (ctypes.c_void_p, []),
        "archive_read_support_filter_all": (ctypes.c_int, [ctypes.c_void_p]),
        "archive_read_support_format_all": (ctypes.c_int, [ctypes.c_void_p]),
        "archive_read_open_filename": (
            ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]),
        "archive_read_next_header": (
            ctypes.c_int, [ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]),
        "archive_entry_pathname": (ctypes.c_char_p, [ctypes.c_void_p]),
        "archive_entry_hardlink": (ctypes.c_char_p, [ctypes.c_void_p]),
        "archive_entry_filetype": (ctypes.c_uint, [ctypes.c_void_p]),
        "archive_read_extract": (
            ctypes.c_int, [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int]),
        "archive_error_string": (ctypes.c_char_p, [ctypes.c_void_p]),
        "archive_read_free": (ctypes.c_int, [ctypes.c_void_p]),
    }
    for name, (result, arguments) in signatures.items():
        function = getattr(archive, name)
        function.restype = result
        function.argtypes = arguments
    return archive


def relative_path(value):
    if not value:
        raise RuntimeError("archive member has no pathname")
    path = PurePosixPath(os.fsdecode(value))
    if path.is_absolute() or ".." in path.parts:
        raise RuntimeError(f"unsafe archive member: {path}")


def extract(archive_path, destination):
    archive = load_archive()
    reader = archive.archive_read_new()
    if not reader:
        raise RuntimeError("cannot allocate libarchive reader")

    def check(result):
        if result != 0:
            error = archive.archive_error_string(reader)
            raise RuntimeError(os.fsdecode(error) if error else f"libarchive error {result}")

    # Preserve modes/times, but never follow unsafe paths or restore ownership.
    # Values are ARCHIVE_EXTRACT_{PERM,TIME,SECURE_SYMLINKS,
    # SECURE_NODOTDOT,SECURE_NOABSOLUTEPATHS} from archive.h.
    flags = 0x0002 | 0x0004 | 0x0100 | 0x0200 | 0x10000
    previous_directory = Path.cwd()
    archive_path = Path(archive_path).resolve()
    destination = Path(destination).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    try:
        check(archive.archive_read_support_filter_all(reader))
        check(archive.archive_read_support_format_all(reader))
        check(archive.archive_read_open_filename(reader, os.fsencode(archive_path), 10240))
        os.chdir(destination)
        entry = ctypes.c_void_p()
        while True:
            result = archive.archive_read_next_header(reader, ctypes.byref(entry))
            if result == 1:  # ARCHIVE_EOF
                break
            check(result)
            relative_path(archive.archive_entry_pathname(entry))
            hardlink = archive.archive_entry_hardlink(entry)
            if hardlink:
                relative_path(hardlink)
            kind = archive.archive_entry_filetype(entry)
            if kind not in (stat.S_IFREG, stat.S_IFDIR, stat.S_IFLNK) and not hardlink:
                raise RuntimeError("dependency archive contains an unsupported special file")
            check(archive.archive_read_extract(reader, entry, flags))
    finally:
        os.chdir(previous_directory)
        archive.archive_read_free(reader)


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("archive", nargs="?")
    parser.add_argument("destination", nargs="?", default=".")
    arguments = parser.parse_args()
    try:
        if arguments.check:
            load_archive()
        else:
            if not arguments.archive:
                parser.error("an RPM archive is required")
            extract(arguments.archive, arguments.destination)
    except (OSError, RuntimeError, AttributeError) as error:
        print(f"[extract_rpm.py] {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
