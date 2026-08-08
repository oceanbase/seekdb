#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Deterministic hang diagnostics for Windows CI / embedded FFI tests.

Enable with env SEEKDB_BINDING_HANG_PROBE=1 (set by run-libseekdb-binding-tests.ps1).
Writes SEEKDB_PROBE_LOG (absolute path, UTF-8 lines: time_ns pid step) with fsync after each line.
Stdout can reorder or buffer on CI; this file reflects actual execution order on disk.
"""
import os
import tempfile
import time
from typing import Optional

_ENV_ENABLE = "SEEKDB_BINDING_HANG_PROBE"
_ENV_LOGPATH = "SEEKDB_PROBE_LOG"


def is_enabled() -> bool:
    return os.environ.get(_ENV_ENABLE) == "1"


def init_probe_log() -> Optional[str]:
    """Create probe log path and set SEEKDB_PROBE_LOG. Prints ::notice:: once with path."""
    if not is_enabled():
        return None
    if os.environ.get(_ENV_LOGPATH):
        return os.environ[_ENV_LOGPATH]
    root = os.environ.get("TEMP") or os.environ.get("TMP") or tempfile.gettempdir()
    path = os.path.join(root, f"seekdb_binding_hang_probe_{os.getpid()}.log")
    os.environ[_ENV_LOGPATH] = os.path.abspath(path)
    try:
        with open(path, "w", encoding="utf-8") as f:
            f.write(f"# seekdb binding hang probe pid={os.getpid()}\n")
            f.flush()
            os.fsync(f.fileno())
    except Exception:
        pass
    print(f"::notice::Hang probe log (use if CI stalls; ordered by real execution): {os.environ[_ENV_LOGPATH]}")
    return os.environ[_ENV_LOGPATH]


def emit(step: str) -> None:
    """Append one probe line and sync to disk."""
    if not is_enabled():
        return
    path = os.environ.get(_ENV_LOGPATH)
    if not path:
        return
    try:
        line = f"{time.time_ns()}\t{os.getpid()}\t{step}\n"
        with open(path, "a", encoding="utf-8") as f:
            f.write(line)
            f.flush()
            os.fsync(f.fileno())
    except Exception:
        pass
