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
python3 - "${seekdb_root}" "${seekdb_prefix}" <<'PYBUILD'
import hashlib
import filecmp
import json
from pathlib import Path
import subprocess
import sys
import tarfile

repo, prefix = map(Path, sys.argv[1:])
spec = json.loads((repo / "tools/wasm/dependencies.json").read_text())["icu"]
root = prefix / "sources"
root.mkdir(parents=True, exist_ok=True)
archive = root / (spec["source_directory"] + ".tgz")
if not archive.exists():
    download = archive.with_suffix(".download")
    subprocess.run(["curl", "--fail", "--location", "--connect-timeout", "20",
                    "--max-time", "180", spec["url"], "--output", str(download)], check=True)
    if hashlib.sha256(download.read_bytes()).hexdigest() != spec["sha256"]:
        raise SystemExit("ICU source checksum mismatch")
    download.replace(archive)
if hashlib.sha256(archive.read_bytes()).hexdigest() != spec["sha256"]:
    raise SystemExit("ICU source checksum mismatch")
source_root = root / spec["source_directory"]
with tarfile.open(archive) as files:
    files.extractall(source_root, filter="data")
source = source_root / "icu/source"
build = prefix / "build/icu"
build.mkdir(parents=True, exist_ok=True)
data = (source / "data/in/icudt69l.dat").read_bytes()
if hashlib.sha256(data).hexdigest() != spec["data_sha256"]:
    raise SystemExit("ICU Unicode data checksum mismatch")
# The official little-endian OffsetTOC package is embedded without conversion.
# Its external C symbol and alignment match ICU's common-data entry point.
# C string chunks keep the compiler's syntax tree small for the 27 MiB data.
data_c = build / "icudt69l.c"
generated = data_c.with_suffix(".c.tmp")
with generated.open("w") as out:
    out.write('_Alignas(16) const unsigned char icudt69_dat[] =\n')
    for start in range(0, len(data), 256):
        out.write('"' + ''.join('\\x%02x' % b for b in data[start:start+256]) + '"\n')
    out.write(';\n')
if data_c.exists() and filecmp.cmp(generated, data_c, shallow=False):
    generated.unlink()
else:
    generated.replace(data_c)
subprocess.run(["emcmake", "cmake", "-S", str(repo / "tools/wasm/icu"), "-B", str(build),
                "-DCMAKE_BUILD_TYPE=MinSizeRel", "-DCMAKE_INSTALL_PREFIX=" + str(prefix),
                "-DICU_SOURCE=" + str(source), "-DICU_DATA_C=" + str(data_c)], check=True)
subprocess.run(["cmake", "--build", str(build), "--parallel", "3"], check=True)
subprocess.run(["cmake", "--install", str(build)], check=True)
PYBUILD
