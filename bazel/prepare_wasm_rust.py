#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
"""Prepare the Wasm Rust patches inside Cargo's private action directory."""

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


def patched_source(root, spec, name):
    data = (root / spec["path"]).read_bytes()
    if hashlib.sha256(data).hexdigest() != spec["input_sha256"]:
        raise SystemExit(name + " patch input changed")
    for before, after in spec["replacements"]:
        if data.count(before.encode()) != 1:
            raise SystemExit(name + " patch context changed")
        data = data.replace(before.encode(), after.encode())
    if hashlib.sha256(data).hexdigest() != spec["output_sha256"]:
        raise SystemExit(name + " patch output changed")
    return data


def prepare_sysroot(original, destination, spec):
    source = original / "lib/rustlib/src"
    data = patched_source(source, spec, "Rust standard library")
    for parent, excluded in (("lib", "rustlib"), ("lib/rustlib", "src")):
        target_dir = destination / parent
        target_dir.mkdir(parents=True)
        for entry in (original / parent).iterdir():
            if entry.name != excluded:
                (target_dir / entry.name).symlink_to(entry, target_is_directory=entry.is_dir())
    target = destination / "lib/rustlib/src"
    shutil.copytree(source, target)
    (target / spec["path"]).write_bytes(data)


def prepare_workspace(repo, build, spec):
    workspace = build / "workspace"
    shutil.copytree(repo / "rust", workspace, ignore=shutil.ignore_patterns(".cargo"))
    metadata = json.loads(subprocess.check_output([
        "cargo", "metadata", "--locked", "--offline", "--format-version", "1",
        "--manifest-path", str(workspace / "Cargo.toml"),
    ], cwd=workspace, universal_newlines=True))
    package = next(package for package in metadata["packages"]
                   if package["name"] == "spdlog-rs" and package["version"] == spec["version"])
    original = Path(package["manifest_path"]).parent
    data = patched_source(original, spec, "spdlog-rs Wasm")
    dependency = build / "patched-crates/spdlog-rs"
    shutil.copytree(original, dependency)
    (dependency / spec["path"]).write_bytes(data)
    (workspace / ".cargo").mkdir()
    (workspace / ".cargo/config.toml").write_text(
        '[patch.crates-io]\nspdlog-rs = { path = ' + json.dumps(str(dependency)) + ' }\n'
    )
    # Resolve the path patch offline before the subsequent --locked build.
    subprocess.run([
        "cargo", "metadata", "--offline", "--format-version", "1",
        "--manifest-path", str(workspace / "Cargo.toml"),
    ], cwd=workspace, check=True, stdout=subprocess.DEVNULL)


if __name__ == "__main__":
    repo = Path(__file__).resolve().parents[1]
    original, build = (Path(argument).resolve() for argument in sys.argv[1:])
    patches = json.loads((repo / "deps/init/wasm/dependencies.json").read_text())
    prepare_sysroot(original, build / "sysroot", patches["rust_std_tls_patch"])
    prepare_workspace(repo, build, patches["spdlog_rs_emscripten"])
