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
python3 - "${seekdb_root}" "${seekdb_prefix}" <<'PY'
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tarfile

repo, prefix = map(Path, sys.argv[1:])
manifest = json.loads((repo / "tools/wasm/dependencies.json").read_text())
root = prefix / "sources"
root.mkdir(parents=True, exist_ok=True)
for name in ("abseil", "s2"):
    spec = manifest[name]
    archive = root / (spec["source_directory"] + ".tar.gz")
    if not archive.exists():
        download = archive.with_suffix(".download")
        subprocess.run(["curl", "--fail", "--location", "--connect-timeout", "20",
                        "--max-time", "180", spec["url"], "--output", str(download)], check=True)
        if hashlib.sha256(download.read_bytes()).hexdigest() != spec["sha256"]:
            raise SystemExit(name + " source checksum mismatch")
        download.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != spec["sha256"]:
        raise SystemExit(name + " source checksum mismatch")
    with tarfile.open(archive) as files:
        files.extractall(root, filter="data")

spec = importlib.util.spec_from_file_location("headers", repo / "tools/wasm/prepare-headers.py")
headers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(headers)
s2 = root / manifest["s2"]["source_directory"]
headers.apply_header(s2 / "src", manifest["s2_wasm_port"], [(
    b"#elif defined(__GLIBC__) || defined(__BIONIC__) || defined(__ASYLO__) || ",
    b"#elif defined(__EMSCRIPTEN__) || defined(__GLIBC__) || defined(__BIONIC__) || defined(__ASYLO__) || ")])
# All consumers use C++20. Abseil chooses std::string_view/optional based on
# the language standard; the upstream S2 hardcoded C++11 would change its ABI.
headers.apply_header(s2, manifest["s2_cpp20"], [(
    b"set(CMAKE_CXX_STANDARD 11)", b"set(CMAKE_CXX_STANDARD 20)")])

def build(source, name, options):
    output = prefix / "build" / name
    subprocess.run(["emcmake", "cmake", "-S", str(source), "-B", str(output),
                    "-DCMAKE_BUILD_TYPE=MinSizeRel", "-DCMAKE_INSTALL_PREFIX=" + str(prefix),
                    "-DCMAKE_CXX_STANDARD=20", "-DCMAKE_CXX_FLAGS=-pthread",
                    "-DBUILD_SHARED_LIBS=OFF", *options], check=True)
    subprocess.run(["cmake", "--build", str(output), "--parallel", "3"], check=True)
    subprocess.run(["cmake", "--install", str(output)], check=True)

build(root / manifest["abseil"]["source_directory"], "absl", [
    "-DABSL_BUILD_TESTING=OFF", "-DABSL_ENABLE_INSTALL=ON", "-DABSL_PROPAGATE_CXX_STD=ON"])
build(s2, "s2", [
    "-DBUILD_EXAMPLES=OFF", "-DWITH_PYTHON=OFF", "-DWITH_GFLAGS=OFF", "-DWITH_GLOG=OFF",
    "-Dabsl_DIR=" + str(prefix / "lib/cmake/absl"),
    "-DOPENSSL_INCLUDE_DIR=" + str(prefix / "include"),
    "-DOPENSSL_CRYPTO_LIBRARY=" + str(prefix / "lib/libcrypto.a"),
    "-DOPENSSL_SSL_LIBRARY=" + str(prefix / "lib/libssl.a")])
PY
