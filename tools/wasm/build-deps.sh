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
# Download only the locked official archive and reject changed contents before
# extraction. Python 3.12+ data filtering prevents archive path traversal.
python3 - "${seekdb_root}/tools/wasm/dependencies.json" "${seekdb_prefix}" <<'PY'
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tarfile

spec = json.loads(Path(sys.argv[1]).read_text())["openssl"]
root = Path(sys.argv[2]) / "sources"
root.mkdir(parents=True, exist_ok=True)
archive = root / (spec["source_directory"] + ".tar.gz")
if not archive.exists():
    download = archive.with_suffix(".download")
    subprocess.run(["curl", "--fail", "--location", "--connect-timeout", "20",
                    "--max-time", "180", spec["url"], "--output", str(download)], check=True)
    if hashlib.sha256(download.read_bytes()).hexdigest() != spec["sha256"]:
        raise SystemExit("OpenSSL source checksum mismatch")
    download.replace(archive)
if hashlib.sha256(archive.read_bytes()).hexdigest() != spec["sha256"]:
    raise SystemExit("OpenSSL source checksum mismatch")
if not (root / spec["source_directory"]).exists():
    with tarfile.open(archive) as source:
        source.extractall(root, filter="data")
PY
seekdb_openssl_source="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["openssl"]["source_directory"])' "${seekdb_root}/tools/wasm/dependencies.json")"
cd "${seekdb_prefix}/sources/${seekdb_openssl_source}"
CC=emcc AR=emar RANLIB=emranlib perl Configure linux-generic32 \
  no-asm no-shared no-tests no-dso no-engine no-async no-sock no-afalgeng no-ui-console \
  -pthread --prefix="${seekdb_prefix}"
make -j4 build_libs
make install_dev
bash "${seekdb_root}/tools/wasm/build-sqlite.sh" "${seekdb_prefix}"
bash "${seekdb_root}/tools/wasm/build-roaring.sh" "${seekdb_prefix}"
bash "${seekdb_root}/tools/wasm/build-libxml2.sh" "${seekdb_prefix}"
bash "${seekdb_root}/tools/wasm/build-icu.sh" "${seekdb_prefix}"
bash "${seekdb_root}/tools/wasm/build-s2.sh" "${seekdb_prefix}"
bash "${seekdb_root}/tools/wasm/build-protobuf-c.sh" "${seekdb_prefix}"
bash "${seekdb_root}/tools/wasm/build-vsag.sh" "${seekdb_prefix}"
