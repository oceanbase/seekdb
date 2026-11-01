#!/usr/bin/env python3
"""Snapshot installed tool locations and vendor locked Cargo dependencies."""

import json
import os
import platform
from pathlib import Path
import shutil
import subprocess
import sys
import sysconfig
import tarfile

def discover(arguments):
    output = Path(arguments[0])
    rustup, native_channel, wasm_channel = arguments[1:]
    exe = ".exe" if os.name == "nt" else ""
    installed = subprocess.check_output([rustup, "toolchain", "list"], text=True)
    channels = {"native": native_channel}
    if any(line.startswith(wasm_channel + "-") for line in installed.splitlines()):
        channels["wasm"] = wasm_channel

    config = {"exe": exe, "python": str(Path(sys.executable).resolve()), "python_libs": {}, "rust": {}}
    host_os = {"linux": "linux", "darwin": "macos", "win32": "windows"}[sys.platform]
    host_cpu = {"x86_64": "x86_64", "amd64": "x86_64", "arm64": "aarch64", "aarch64": "aarch64"}[platform.machine().lower()]
    config["execution_constraints"] = ["@platforms//os:" + host_os, "@platforms//cpu:" + host_cpu]
    stdlib = Path(sysconfig.get_path("stdlib"))
    if os.name == "nt":
        config["python_libs"] = {"Lib": str(stdlib), "DLLs": str(Path(sys.base_prefix) / "DLLs")}
        config["python_libs"].update({"bin/" + path.name: str(path) for path in Path(sys.base_prefix).glob("*.dll")})
    else:
        config["python_libs"][stdlib.parent.name + "/" + stdlib.name] = str(stdlib)
        platstdlib = Path(sysconfig.get_path("platstdlib"))
        if platstdlib != stdlib:
            config["python_libs"]["lib64/" + platstdlib.name] = str(platstdlib)

    for name, channel in channels.items():
        cargo = Path(subprocess.check_output([rustup, "which", "cargo", "--toolchain", channel], text=True).strip())
        config["rust"][name] = str(cargo.parent.parent)
    (output / "toolchains.json").write_text(json.dumps(config))


def vendor(arguments):
    root, output = map(Path, arguments[:2])
    metadata, name = arguments[2:]
    config = json.loads(Path(metadata).read_text())
    exe = config["exe"]
    sysroot = Path(config["rust"][name])
    cargo = sysroot / "bin" / ("cargo" + exe)
    vendor = output / "vendor"
    # A registry alias avoids conflicting with existing names for the same mirror.
    command = [str(cargo), "vendor", "--locked", "--respect-source-config",
               "--config", 'source.crates-io.replace-with="seekdb"',
               "--config", 'registries.seekdb.index="sparse+https://rsproxy.cn/index/"',
               "--manifest-path", str(root / "rust/Cargo.toml"),
               "--sync", str(root / "deps/external/Cargo.toml")]
    if name == "wasm":
        command += ["--sync", str(sysroot / "lib/rustlib/src/rust/library/sysroot/Cargo.toml")]
    command.append(str(vendor))
    environment = os.environ.copy()
    environment["RUSTC"] = str(sysroot / "bin" / ("rustc" + exe))
    # Downloads happen only during repository fetching, using Cargo's existing
    # cache and the project mirror. Build actions below always run offline.
    subprocess.run(command, cwd=root / "rust", env=environment, check=True, stdout=subprocess.DEVNULL)
    # One input archive avoids exposing vendored BUILD files as Bazel packages.
    # Normalize archive metadata so identical sources have identical digests.
    with tarfile.open(output / "vendor.tar", "w") as archive:
        for path in sorted(vendor.rglob("*")):
            if not path.is_file():
                continue
            info = archive.gettarinfo(str(path), str(path.relative_to(vendor)))
            info.type = tarfile.REGTYPE
            info.linkname = ""
            info.size = path.stat().st_size
            info.uid = info.gid = info.mtime = 0
            info.uname = info.gname = ""
            with path.open("rb") as source:
                archive.addfile(info, source)
    shutil.rmtree(vendor)


if __name__ == "__main__":
    {"discover": discover, "vendor": vendor}[sys.argv[1]](sys.argv[2:])
