#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
"""Stage a private standard-library source patch; never edit rustup's sysroot."""
import hashlib
import json
from pathlib import Path
import sys

repo = Path(__file__).resolve().parents[2]
original, destination = map(lambda value: Path(value).resolve(), sys.argv[1:])
if original == destination:
    raise SystemExit("The patched sysroot must be separate from rustup")
spec = json.loads((repo / "tools/wasm/dependencies.json").read_text())["rust_std_tls_patch"]
source = original / "lib/rustlib/src" / spec["path"]
data = source.read_bytes()
if hashlib.sha256(data).hexdigest() != spec["input_sha256"]:
    raise SystemExit("Rust standard library patch input changed")
for before, after in spec["replacements"]:
    if data.count(before.encode()) != 1:
        raise SystemExit("Rust standard library patch context changed")
    data = data.replace(before.encode(), after.encode())
if hashlib.sha256(data).hexdigest() != spec["output_sha256"]:
    raise SystemExit("Rust standard library patch output changed")

# Compiler/host libraries remain those from the pinned rustup toolchain.
for parent, excluded in (("lib", "rustlib"), ("lib/rustlib", "src")):
    target_dir = destination / parent
    target_dir.mkdir(parents=True, exist_ok=True)
    for entry in (original / parent).iterdir():
        if entry.name != excluded:
            target = target_dir / entry.name
            if not target.is_symlink():
                target.symlink_to(entry, target_is_directory=entry.is_dir())
            elif target.resolve() != entry.resolve():
                raise SystemExit("Patched sysroot points to a different toolchain")

# Preserve timestamps when repeating preparation, including for the patched
# file, so Cargo does not rebuild the entire standard library on every run.
src_root = original / "lib/rustlib/src"
for entry in src_root.rglob("*"):
    if entry.is_file():
        target = destination / "lib/rustlib/src" / entry.relative_to(src_root)
        content = data if entry == source else entry.read_bytes()
        if not target.exists() or target.read_bytes() != content:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)
