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
import tarfile

spec = json.loads(Path(sys.argv[1]).read_text())["libxml2"]
prefix = Path(sys.argv[2])
root = prefix / "sources"
root.mkdir(parents=True, exist_ok=True)
archive = root / (spec["source_directory"] + ".tar.xz")
if not archive.exists():
    download = archive.with_suffix(".download")
    subprocess.run(["curl", "--fail", "--location", "--connect-timeout", "20",
                    "--max-time", "180", spec["url"], "--output", str(download)], check=True)
    if hashlib.sha256(download.read_bytes()).hexdigest() != spec["sha256"]:
        raise SystemExit("libxml2 archive checksum mismatch")
    download.replace(archive)
if hashlib.sha256(archive.read_bytes()).hexdigest() != spec["sha256"]:
    raise SystemExit("libxml2 archive checksum mismatch")
source = root / spec["source_directory"]
with tarfile.open(archive) as files:
    files.extractall(root, filter="data")
build = prefix / "build" / "libxml2"
subprocess.run(["emcmake", "cmake", "-S", str(source), "-B", str(build),
                "-DCMAKE_BUILD_TYPE=MinSizeRel", "-DCMAKE_INSTALL_PREFIX=" + str(prefix),
                "-DCMAKE_C_FLAGS=-pthread", "-DBUILD_SHARED_LIBS=OFF",
                "-DLIBXML2_WITH_THREADS=ON", "-DLIBXML2_WITH_ICONV=ON",
                "-DLIBXML2_WITH_PYTHON=OFF", "-DLIBXML2_WITH_PROGRAMS=OFF",
                "-DLIBXML2_WITH_TESTS=OFF", "-DLIBXML2_WITH_HTTP=OFF",
                "-DLIBXML2_WITH_FTP=OFF", "-DLIBXML2_WITH_LZMA=OFF",
                "-DLIBXML2_WITH_ZLIB=OFF", "-DLIBXML2_WITH_MODULES=OFF"], check=True)
subprocess.run(["cmake", "--build", str(build), "--parallel", "3"], check=True)
subprocess.run(["cmake", "--install", str(build)], check=True)
PY
