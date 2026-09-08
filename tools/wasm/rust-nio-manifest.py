#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
"""Reject stale externally built NIO archives when CMake builds the engine."""
import argparse
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("build_dir", type=Path)
parser.add_argument("--record", action="store_true")
args = parser.parse_args()
repo = Path(__file__).resolve().parents[2]
build = args.build_dir.resolve()
archive = build / "target/wasm32-unknown-emscripten/release/libsql_nio.a"
manifest = build / "nio-build-manifest.json"
paths = [
    *sorted((repo / "rust/sql-nio/src").glob("*.rs")),
    *sorted((repo / "rust/sql-nio/include").glob("*.h")),
    *(repo / name for name in (
        "rust/Cargo.toml", "rust/Cargo.lock", "rust/sql-nio/Cargo.toml",
        "tools/wasm/rust-toolchain-version", "tools/wasm/emscripten-version",
        "tools/wasm/build-rust-nio.sh", "tools/wasm/rust-build-env.sh",
        "tools/wasm/prepare-rust-sysroot.py", "tools/wasm/rust-nio-manifest.py",
        "unittest/wasm/test_wasm_nio_memory.cpp",
    )),
]

def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()

try:
    std_patch = json.loads((repo / "tools/wasm/dependencies.json").read_text())["rust_std_tls_patch"]
    std_path = build / "sysroot/lib/rustlib/src" / std_patch["path"]
    if digest(std_path) != std_patch["output_sha256"]:
        raise ValueError("private Rust standard-library TLS patch does not match")
    current = {
        "version": 1,
        "target": "wasm32-unknown-emscripten",
        "features": ["memory-transport"],
        "rustflags": "-Ctarget-feature=+atomics,+bulk-memory,+mutable-globals -Cpanic=abort",
        "inputs": {str(path.relative_to(repo)): digest(path) for path in paths},
        "std_patch": std_patch,
        "archive_sha256": digest(archive),
    }
    if args.record:
        manifest.write_text(json.dumps(current, indent=2) + "\n")
    elif json.loads(manifest.read_text()) != current:
        raise ValueError("NIO sources, build configuration, or archive changed")
except (OSError, ValueError, KeyError) as error:
    raise SystemExit(f"{error}; rebuild with bash tools/wasm/build-rust-nio.sh {build}") from error
