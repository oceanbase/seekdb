#!/usr/bin/env bash
# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
seekdb_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
seekdb_prefix="${1:-${seekdb_root}/build_wasm_deps}"
mkdir -p "${seekdb_prefix}"
seekdb_prefix="$(cd "${seekdb_prefix}" && pwd)"
seekdb_version="$(cat "${seekdb_root}/tools/wasm/emscripten-version")"
if ! emcc --version | head -n 1 | grep -Eq " ${seekdb_version//./\\.}( |$)"; then
  echo "Activate Emscripten ${seekdb_version} before building Wasm dependencies." >&2
  exit 1
fi
python3 - "${seekdb_root}/tools/wasm/dependencies.json" "${seekdb_prefix}" <<'PY'
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import zipfile

spec = json.loads(Path(sys.argv[1]).read_text())["sqlite"]
prefix = Path(sys.argv[2])
root = prefix / "sources"
root.mkdir(parents=True, exist_ok=True)
archive = root / (spec["source_directory"] + ".zip")
if not archive.exists():
    download = archive.with_suffix(".download")
    subprocess.run(["curl", "--fail", "--location", "--connect-timeout", "20",
                    "--max-time", "180", spec["url"], "--output", str(download)], check=True)
    if hashlib.sha256(download.read_bytes()).hexdigest() != spec["sha256"]:
        raise SystemExit("SQLite archive checksum mismatch")
    download.replace(archive)
if hashlib.sha256(archive.read_bytes()).hexdigest() != spec["sha256"]:
    raise SystemExit("SQLite archive checksum mismatch")
source = root / spec["source_directory"]
source.mkdir(exist_ok=True)
# Extract only the three known files; cached source changes cannot bypass the
# archive check. Verify the amalgamation against the official release's SHA3.
with zipfile.ZipFile(archive) as files:
    for name in ("sqlite3.c", "sqlite3.h", "sqlite3ext.h"):
        content = files.read(spec["source_directory"] + "/" + name)
        if name == "sqlite3.c" and hashlib.sha3_256(content).hexdigest() != spec["source_sha3_256"]:
            raise SystemExit("SQLite amalgamation checksum mismatch")
        (source / name).write_bytes(content)
obj = source / "sqlite3.o"
# Keep real mutexes and WAL support. The Unix VFS is only an intermediate MEMFS
# backend; it is not an OPFS implementation or evidence of durable commits.
subprocess.run(["emcc", "-Oz", "-pthread", "-DSQLITE_THREADSAFE=1",
                "-DSQLITE_OMIT_LOAD_EXTENSION=1", "-c", str(source / "sqlite3.c"),
                "-o", str(obj)], check=True)
lib = prefix / "lib"
lib.mkdir(exist_ok=True)
subprocess.run(["emar", "rcs", str(lib / "libsqlite3.a"), str(obj)], check=True)
headers = prefix / "include" / "sqlite"
headers.mkdir(parents=True, exist_ok=True)
for name in ("sqlite3.h", "sqlite3ext.h"):
    (headers / name).write_bytes((source / name).read_bytes())
PY
