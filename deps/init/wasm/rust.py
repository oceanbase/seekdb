#!/usr/bin/env python3
"""Prepare and validate the pinned Emscripten Rust standard-library SDK."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile


HERE = Path(__file__).resolve().parent


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_digest(root):
    result = hashlib.sha256()
    for path in sorted(root.rglob("*")):
        if path.is_file():
            result.update(path.relative_to(root).as_posix().encode() + b"\0")
            result.update(bytes.fromhex(digest(path)))
    return result.hexdigest()


def configuration():
    dependencies = json.loads((HERE / "dependencies.json").read_text())
    return dependencies["rust_std_tls_patch"], dependencies["rust_std_build"]


def identity(sysroot):
    patch, build = configuration()
    rustc = sysroot / "bin/rustc"
    version = subprocess.check_output([str(rustc), "-vV"], universal_newlines=True).strip()
    if version.splitlines()[0] != patch["rustc"]:
        raise SystemExit("Wasm Rust compiler does not match the standard-library patch")
    fields = dict(line.split(": ", 1) for line in version.splitlines() if ": " in line)
    source = sysroot / "lib/rustlib/src/rust"
    return {
        "compiler": version,
        "host": fields["host"],
        "target": build["target"],
        "source_sha256": source_digest(source / "library"),
        "lock_sha256": digest(source / "library/Cargo.lock"),
        "patch": patch,
        "build": build,
        "emscripten": (HERE / "emscripten-version").read_text().strip(),
        "prepare_sha256": digest(Path(__file__)),
    }


def sdk_key(inputs):
    return hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()


def inspect(prefix, sysroot):
    inputs = identity(sysroot)
    key = sdk_key(inputs)
    sdk = prefix / "rust" / key
    manifest = sdk / "sdk.json"
    if not manifest.is_file():
        raise SystemExit("Missing or stale Wasm Rust standard library; run ./build.py init --wasm")
    metadata = json.loads(manifest.read_text())
    if metadata["inputs"] != inputs:
        raise SystemExit("Wasm Rust SDK identity changed; run ./build.py init --wasm")
    for name in metadata["files"]:
        if not (sdk / name).is_file():
            raise SystemExit("Incomplete Wasm Rust SDK; run ./build.py init --wasm")
    return sdk, metadata


def patch_source(sysroot, destination, patch):
    original = sysroot / "lib/rustlib/src"
    data = (original / patch["path"]).read_bytes()
    if hashlib.sha256(data).hexdigest() != patch["input_sha256"]:
        raise SystemExit("Rust standard library patch input changed")
    for before, after in patch["replacements"]:
        if data.count(before.encode()) != 1:
            raise SystemExit("Rust standard library patch context changed")
        data = data.replace(before.encode(), after.encode())
    if hashlib.sha256(data).hexdigest() != patch["output_sha256"]:
        raise SystemExit("Rust standard library patch output changed")
    for parent, excluded in (("lib", "rustlib"), ("lib/rustlib", "src")):
        directory = destination / parent
        directory.mkdir(parents=True)
        for entry in (sysroot / parent).iterdir():
            if entry.name != excluded:
                (directory / entry.name).symlink_to(entry, target_is_directory=entry.is_dir())
    target = destination / "lib/rustlib/src"
    shutil.copytree(original, target)
    (target / patch["path"]).write_bytes(data)


def prepare(prefix, sysroot, jobs):
    inputs = identity(sysroot)
    key = sdk_key(inputs)
    sdk = prefix / "rust" / key
    if (sdk / "sdk.json").is_file():
        _, metadata = inspect(prefix, sysroot)
        if all(digest(sdk / name) == checksum for name, checksum in metadata["files"].items()):
            print("Reusing Wasm Rust standard library " + key[:12], flush=True)
            return sdk
        raise SystemExit("Wasm Rust SDK files changed: " + str(sdk))
    sdk.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="prepare.", dir=sdk.parent) as temporary:
        work = Path(temporary)
        patched = work / "sysroot"
        patch_source(sysroot, patched, inputs["patch"])
        workspace = work / "workspace"
        workspace.mkdir()
        profile = inputs["build"]["profile"]
        (workspace / "Cargo.toml").write_text(
            '[package]\nname = "std-sdk"\nversion = "0.0.0"\nedition = "2021"\n'
            '[lib]\npath = "lib.rs"\n[workspace]\n[profile.release]\n' +
            "\n".join(name + " = " + json.dumps(value) for name, value in profile.items()) + "\n"
        )
        (workspace / "lib.rs").write_text("#![no_std]\n")
        environment = os.environ.copy()
        # Cargo locates build-std sources through the host compiler's sysroot;
        # target RUSTFLAGS alone do not select the patched rust-src copy.
        compiler = work / "rustc"
        compiler.write_text("#!/bin/sh\nexec " + shlex.quote(str(sysroot / "bin/rustc")) +
                            " " + shlex.quote("--sysroot=" + str(patched)) + ' "$@"\n')
        compiler.chmod(0o755)
        environment["RUSTC"] = str(compiler)
        environment["RUSTUP_TOOLCHAIN"] = inputs["patch"]["toolchain"]
        environment.pop("RUSTFLAGS", None)
        environment["CARGO_ENCODED_RUSTFLAGS"] = "\x1f".join(inputs["build"]["rustflags"])
        cargo = [str(sysroot / "bin/cargo"),
                 "--config", 'source.crates-io.replace-with="seekdb"',
                 "--config", 'registries.seekdb.index="sparse+https://rsproxy.cn/index/"']
        subprocess.run(cargo + ["fetch", "--locked", "--manifest-path",
                               str(patched / "lib/rustlib/src/rust/library/sysroot/Cargo.toml")],
                       cwd=workspace, env=environment, check=True)
        command = cargo + ["build", "--offline", "--release", "--target", inputs["target"],
                           "--target-dir", str(work / "target"), "-j", str(jobs),
                           "-Zbuild-std=std,panic_abort", "--message-format=json-render-diagnostics"]
        artifacts = set()
        checked_std = False
        with subprocess.Popen(command, cwd=workspace, env=environment, stdout=subprocess.PIPE,
                              universal_newlines=True) as process:
            for line in process.stdout:
                message = json.loads(line)
                if message["reason"] != "compiler-artifact":
                    continue
                if message["target"]["name"] == "std":
                    source = Path(message["target"]["src_path"])
                    if patched not in source.parents:
                        raise SystemExit("Cargo compiled std from unpatched sources: " + str(source))
                    checked_std = True
                    print("Compiled patched std: " + str(source), flush=True)
                if message["target"]["name"] == "std_sdk":
                    continue
                for name in message["filenames"]:
                    path = Path(name)
                    if inputs["target"] in path.parts and path.suffix in (".rlib", ".rmeta"):
                        artifacts.add(path)
                        metadata = path.with_suffix(".rmeta")
                        if metadata.is_file():
                            artifacts.add(metadata)
            if process.wait():
                raise SystemExit("Wasm Rust standard library compilation failed")
        if not checked_std:
            raise SystemExit("Cargo did not report building the patched standard library")
        output = work / "sdk"
        library = output / "lib/rustlib" / inputs["target"] / "lib"
        library.mkdir(parents=True)
        for path in sorted(artifacts):
            shutil.copyfile(path, library / path.name)
        for name in ("std", "core", "alloc", "compiler_builtins", "panic_abort"):
            if not list(library.glob("lib" + name + "-*.rlib")):
                raise SystemExit("Missing standard library artifact: " + name)
        metadata = {"inputs": inputs, "files": {
            path.relative_to(output).as_posix(): digest(path) for path in sorted(library.iterdir())
        }}
        (output / "sdk.json").write_text(json.dumps(metadata, indent=2) + "\n")
        if not sdk.exists():
            output.rename(sdk)
    print("Prepared Wasm Rust standard library " + key[:12], flush=True)
    return sdk


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["prepare", "inspect"])
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--sysroot", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    args = parser.parse_args()
    if args.command == "prepare":
        prepare(args.prefix.resolve(), args.sysroot.resolve(), args.jobs)
    else:
        sdk, metadata = inspect(args.prefix.resolve(), args.sysroot.resolve())
        print(json.dumps({"sdk": str(sdk), "inputs": metadata["inputs"]}))


if __name__ == "__main__":
    main()
