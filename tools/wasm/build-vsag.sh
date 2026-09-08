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
python3 - "${seekdb_root}" "${seekdb_prefix}" "${2:-build}" <<'PY'
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tarfile

repo, prefix = map(Path, sys.argv[1:3])
mode = sys.argv[3]
if mode not in ("build", "--prepare-only"):
    raise SystemExit("Usage: build-vsag.sh [prefix] [--prepare-only]")
specs = json.loads((repo / "tools/wasm/dependencies.json").read_text())
root = prefix / "sources"
root.mkdir(parents=True, exist_ok=True)
sources = {}
for name in ("vsag", "cpuinfo", "fmt", "spdlog", "nlohmann_json", "robin_map", "thread_pool", "clapack", "openblas", "antlr4"):
    spec = specs[name]
    archive = root / (spec["source_directory"] + ".tar.gz")
    if not archive.exists():
        download = archive.with_suffix(".download")
        subprocess.run(["curl", "--fail", "--location", "--connect-timeout", "20",
                        "--max-time", "180", spec["url"], "--output", str(download)], check=True)
        if hashlib.sha256(download.read_bytes()).hexdigest() != spec["sha256"]:
            raise SystemExit(f"{name} archive checksum mismatch")
        download.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != spec["sha256"]:
        raise SystemExit(f"{name} archive checksum mismatch")
    # Copy only changed contents, so repeated dependency builds are incremental.
    with tarfile.open(archive) as files:
        for member in files:
            if member.isfile():
                target = root / member.name
                if not target.resolve().is_relative_to(root.resolve()):
                    raise SystemExit("Unsafe dependency archive path")
                data = files.extractfile(member).read()
                for patch in specs.get("vsag_blas_patches", {}).get("files", []):
                    if name == patch["dependency"] and member.name == spec["source_directory"] + "/" + patch["path"]:
                        if hashlib.sha256(data).hexdigest() != patch["input_sha256"]:
                            raise SystemExit("VSAG BLAS input changed")
                        for before, after in patch["replacements"]:
                            if data.count(before.encode()) != 1:
                                raise SystemExit("VSAG BLAS patch context changed")
                            data = data.replace(before.encode(), after.encode())
                        if hashlib.sha256(data).hexdigest() != patch["output_sha256"]:
                            raise SystemExit("VSAG BLAS patch changed")
                if name == "vsag":
                    for patch in specs.get("vsag_source_patches", {}).get("files", []):
                        if member.name == spec["source_directory"] + "/" + patch["path"]:
                            if hashlib.sha256(data).hexdigest() != patch["input_sha256"]:
                                raise SystemExit("VSAG source input changed")
                            for replacement in patch["replacements"]:
                                before, after = (s.encode() for s in replacement)
                                if data.count(before) != 1:
                                    raise SystemExit("VSAG source patch context changed")
                                data = data.replace(before, after)
                            if hashlib.sha256(data).hexdigest() != patch["output_sha256"]:
                                raise SystemExit("VSAG source patch changed")
                if name == "cpuinfo" and member.name == spec["source_directory"] + "/CMakeLists.txt":
                    patch = specs["cpuinfo_wasm_cmake"]
                    if hashlib.sha256(data).hexdigest() != patch["input_sha256"]:
                        raise SystemExit("cpuinfo build input changed")
                    data = data.replace(b"|riscv(32|64))$", b"|riscv(32|64)|wasm32|wasm64)$")
                    data = data.replace(b"Darwin|Linux|Android|FreeBSD)$", b"Darwin|Linux|Android|FreeBSD|Emscripten)$")
                    if hashlib.sha256(data).hexdigest() != patch["output_sha256"]:
                        raise SystemExit("cpuinfo build patch changed")
                if name == "vsag" and member.name == spec["source_directory"] + "/src/dataset_impl.cpp":
                    patch = specs["vsag_dataset_paths"]
                    if hashlib.sha256(data).hexdigest() != patch["input_sha256"]:
                        raise SystemExit("VSAG dataset input changed")
                    before = b'''    auto* paths = new std::string[num_elements];
    copy_dataset->Paths(paths);
    for (int i = 0; i < num_elements; ++i) {
        paths[i] += this->GetPaths()[i];
    }'''
                    after = b'''    if (const auto* source_paths = this->GetPaths(); source_paths != nullptr) {
        auto* paths = new std::string[num_elements];
        copy_dataset->Paths(paths);
        for (int64_t i = 0; i < num_elements; ++i) {
            paths[i] = source_paths[i];
        }
    }'''
                    data = data.replace(before, after)
                    if hashlib.sha256(data).hexdigest() != patch["output_sha256"]:
                        raise SystemExit("VSAG dataset patch changed")
                if name == "vsag" and member.name == spec["source_directory"] + "/src/storage/stream_reader.h":
                    patch = specs["vsag_stream_sizes"]
                    if hashlib.sha256(data).hexdigest() != patch["input_sha256"]:
                        raise SystemExit("VSAG stream input changed")
                    data = data.replace(b"#include <stack>", b"#include <stack>\n#include <stdexcept>")
                    data = data.replace(b'''        std::vector<char> buffer(length);
        reader.Read(buffer.data(), length);
        return {buffer.data(), length};''', b'''        std::string result;
        if (length > result.max_size()) {
            throw std::length_error("VSAG string exceeds target address space");
        }
        result.resize(static_cast<size_t>(length));
        if (length != 0) {
            reader.Read(result.data(), length);
        }
        return result;''')
                    data = data.replace(b'''        val.resize(size);
        reader.Read(reinterpret_cast<char*>(val.data()), size * sizeof(T));''', b'''        if (size > val.max_size() || size > SIZE_MAX / sizeof(T)) {
            throw std::length_error("VSAG vector exceeds target address space");
        }
        val.resize(static_cast<size_t>(size));
        if (size != 0) {
            reader.Read(reinterpret_cast<char*>(val.data()), size * sizeof(T));
        }''')
                    if hashlib.sha256(data).hexdigest() != patch["output_sha256"]:
                        raise SystemExit("VSAG stream patch changed")
                if name == "vsag" and member.name == spec["source_directory"] + "/src/impl/allocator/default_allocator.cpp":
                    patch = specs["vsag_allocator_sizes"]
                    if hashlib.sha256(data).hexdigest() != patch["input_sha256"]:
                        raise SystemExit("VSAG allocator input changed")
                    data = data.replace(b"#include <fmt/format.h>", b"#include <fmt/format.h>\n#include <cstdint>")
                    for signature in (b"DefaultAllocator::Allocate(uint64_t size) {",
                                      b"DefaultAllocator::Reallocate(void* p, uint64_t size) {"):
                        data = data.replace(signature, signature + b'''
    if (size > SIZE_MAX) {
        return nullptr;
    }''')
                    if hashlib.sha256(data).hexdigest() != patch["output_sha256"]:
                        raise SystemExit("VSAG allocator patch changed")
                if not target.exists() or target.read_bytes() != data:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(data)
    sources[name] = root / spec["source_directory"]
if mode == "--prepare-only":
    raise SystemExit(0)
build = prefix / "build/vsag-kernels"
subprocess.run(["emcmake", "cmake", "-S", str(repo / "tools/wasm/vsag"),
                "-B", str(build), "-G", "Unix Makefiles",
                "-DCMAKE_POLICY_VERSION_MINIMUM=3.5", "-DCMAKE_BUILD_TYPE=Release",
                f"-DCMAKE_INSTALL_PREFIX={prefix}", f"-DVSAG_SOURCE={sources['vsag']}",
                f"-DCPUINFO_SOURCE={sources['cpuinfo']}",
                f"-DFMT_SOURCE={sources['fmt']}", f"-DSPDLOG_SOURCE={sources['spdlog']}",
                f"-DJSON_SOURCE={sources['nlohmann_json']}",
                f"-DTHREAD_POOL_SOURCE={sources['thread_pool']}",
                f"-DCLAPACK_SOURCE={sources['clapack']}",
                f"-DOPENBLAS_SOURCE={sources['openblas']}",
                f"-DANTLR4_SOURCE={sources['antlr4']}",
                f"-DROBIN_MAP_SOURCE={sources['robin_map']}"], check=True)
subprocess.run(["cmake", "--build", str(build), "--parallel", "4"], check=True)
subprocess.run(["cmake", "--install", str(build)], check=True)
PY
