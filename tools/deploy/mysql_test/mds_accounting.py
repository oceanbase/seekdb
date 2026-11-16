"""Assert MDS rollback accounting from the native instance exercised by mysqltest."""

import os
import re


def open_log(path):
    if os.name != "nt":
        return path.open("rb")
    # Preserve the logger's ability to rotate while this reader is open.
    import ctypes
    import msvcrt
    from ctypes import wintypes
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                   wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD,
                                   wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    handle = kernel.CreateFileW(str(path), 0x80000000, 7, None, 3, 0, None)
    if handle == wintypes.HANDLE(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        descriptor = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
    except BaseException:
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        kernel.CloseHandle(handle)
        raise
    return os.fdopen(descriptor, "rb")


def snapshot(base_dir):
    with open_log(base_dir / "log" / "seekdb.log") as stream:
        stat = os.fstat(stream.fileno())
        return stat.st_dev, stat.st_ino, stat.st_size


def check(base_dir, start):
    records = 0
    found_start = False
    remaining = 128 * 1024 * 1024
    pattern = re.compile(rb"rollback the last mds node\(ret=0,.*"
                         rb"unsubmitted_size_:(-?\d+), mds_list_\.size\(\):0")
    paths = sorted((base_dir / "log").glob("seekdb.log.*"))
    paths.append(base_dir / "log" / "seekdb.log")
    for path in paths:
        with open_log(path) as stream:
            stat = os.fstat(stream.fileno())
            if not found_start:
                if (stat.st_dev, stat.st_ino) != start[:2]:
                    continue
                if stat.st_size < start[2]:
                    raise RuntimeError("Native log truncated during MDS regression")
                found_start = True
                stream.seek(start[2])
            for line in stream:
                remaining -= len(line)
                if remaining < 0:
                    raise RuntimeError("MDS regression log coverage exceeds 128 MiB")
                match = pattern.search(line)
                if match:
                    records += 1
                    if int(match.group(1)) != 0:
                        raise RuntimeError("MDS rollback left bytes with no nodes: "
                                           + line.decode("utf-8", "replace").strip())
    if not found_start or records == 0:
        raise RuntimeError("MDS regression did not capture an empty-cache rollback")
    return records
