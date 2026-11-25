#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
"""Prepare Wasm headers, the Emscripten sysroot and the Rust standard library."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import shutil
import tempfile

from rust import prepare as prepare_rust_std


def prepare_headers(source, prefix, manifest):
    include = prefix / "include"
    include.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="boost.", dir=include) as temporary:
        staging = Path(temporary)
        shutil.copytree(source / "boost", staging / "boost")
        for patch in manifest["boost_geometry_math"]["headers"]:
            header = staging / patch["path"]
            data = header.read_bytes()
            if hashlib.sha256(data).hexdigest() != patch["input_sha256"]:
                raise SystemExit(f"Unverified dependency header: {header}")
            for old, new in patch["replacements"]:
                if old.encode() not in data:
                    raise SystemExit(f"Unexpected header adaptation context: {header}")
                data = data.replace(old.encode(), new.encode())
            if hashlib.sha256(data).hexdigest() != patch["output_sha256"]:
                raise SystemExit(f"Unexpected header adaptation result: {header}")
            header.write_bytes(data)
        destination = include / "boost"
        if destination.is_symlink():
            destination.unlink()
        elif destination.exists():
            shutil.rmtree(destination)
        (staging / "boost").rename(destination)


def main():
    repo = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("jobs must be positive")
    sdk_version = Path(__file__).with_name("emscripten-version").read_text().strip()
    version_error = f"Activate Emscripten {sdk_version} before running ./build.py init --wasm."
    try:
        compiler = subprocess.run(["emcc", "--version"], stdout=subprocess.PIPE, universal_newlines=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        raise SystemExit(version_error)
    if not re.search(r" " + re.escape(sdk_version) + r"([ -]|$)", compiler.stdout.partition("\n")[0]):
        raise SystemExit(version_error)

    channel = Path(__file__).with_name("rust-toolchain-version").read_text().strip()
    subprocess.run([
        "rustup", "toolchain", "install", channel, "--profile", "minimal",
        "--no-self-update",
    ], cwd=repo, check=True)
    subprocess.run(["rustup", "component", "add", "rust-src", "--toolchain", channel], check=True)
    subprocess.run(["rustup", "target", "add", "wasm32-unknown-emscripten", "--toolchain", channel], check=True)
    prefix = Path(os.environ.get("SEEKDB_WASM_DEPS", repo / "deps/wasm")).resolve()
    prefix.mkdir(parents=True, exist_ok=True)
    os.environ["EM_CACHE"] = str(prefix / "em-cache")
    dependencies = json.loads(Path(__file__).with_name("dependencies.json").read_text())
    prepare_headers(repo / "deps/prebuilt/wasm/include", prefix, dependencies)

    # Bazel consumes this prepared thread/exception ABI cache read-only.
    with tempfile.TemporaryDirectory(prefix="sysroot.", dir=prefix) as temporary:
        probe = Path(temporary)
        (probe / "main.cpp").write_text("int main() { return 0; }\n")
        subprocess.run([
            "emcc", str(probe / "main.cpp"), "-Oz", "-pthread", "-fexceptions", "-msimd128",
            "-sWASMFS", "-sASSERTIONS=2", "-sPROXY_TO_PTHREAD=1", "-sEXIT_RUNTIME=1",
            "-o", str(probe / "main.mjs"),
        ], check=True)
    rustc = Path(subprocess.check_output(
        ["rustup", "which", "rustc", "--toolchain", channel], universal_newlines=True,
    ).strip())
    prepare_rust_std(prefix, rustc.parent.parent, args.jobs)


if __name__ == "__main__":
    main()
