#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
"""Prepare pinned Wasm libraries, headers and the Emscripten sysroot."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import shutil
import tarfile
import tempfile


def locked_source(prefix, spec, suffix, checksum_error, *, reuse=False):
    root = prefix / "sources"
    root.mkdir(parents=True, exist_ok=True)
    archive = root / (spec["source_directory"] + suffix)
    if not archive.exists():
        download = archive.with_suffix(".download")
        subprocess.run([
            "curl", "--fail", "--location", "--connect-timeout", "20",
            "--max-time", "180", spec["url"], "--output", str(download),
        ], check=True)
        if hashlib.sha256(download.read_bytes()).hexdigest() != spec["sha256"]:
            raise SystemExit(checksum_error)
        download.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != spec["sha256"]:
        raise SystemExit(checksum_error)
    source = root / spec["source_directory"]
    if not reuse or not source.exists():
        with tarfile.open(archive) as files:
            files.extractall(root, filter="data")
    return source


def build_openssl(prefix, spec, jobs):
    source = locked_source(prefix, spec, ".tar.gz", "OpenSSL source checksum mismatch", reuse=True)
    subprocess.run([
        "perl", "Configure", "linux-generic32",
        "no-asm", "no-shared", "no-tests", "no-dso", "no-engine", "no-async",
        "no-sock", "no-afalgeng", "no-ui-console", "-pthread", "--prefix=" + str(prefix),
    ], cwd=source, env=dict(os.environ, CC="emcc", AR="emar", RANLIB="emranlib"), check=True)
    subprocess.run(["make", "-j" + str(jobs), "build_libs"], cwd=source, check=True)
    subprocess.run(["make", "install_dev"], cwd=source, check=True)


def build_libxml2(prefix, spec, jobs):
    source = locked_source(prefix, spec, ".tar.xz", "libxml2 archive checksum mismatch")
    build = prefix / "build/libxml2"
    build.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        "emconfigure", str(source / "configure"),
        "--host=wasm32-unknown-emscripten", "--prefix=" + str(prefix),
        "--disable-shared", "--enable-static", "--with-threads",
        "--with-iconv", "--without-python", "--without-http", "--without-ftp",
        "--without-lzma", "--without-zlib", "--without-modules", "CFLAGS=-Oz -pthread",
    ], cwd=build, check=True)
    subprocess.run(["emmake", "make", "-j" + str(jobs), "libxml2.la"], cwd=build, check=True)
    subprocess.run([
        "emmake", "make", "install-libLTLIBRARIES", "install-nodist_pkgconfigDATA",
    ], cwd=build, check=True)
    subprocess.run(["emmake", "make", "-C", "include", "install"], cwd=build, check=True)


def build_protobuf_c(prefix, spec):
    source = locked_source(prefix, spec, ".tar.gz", "protobuf-c archive checksum mismatch")
    obj = source / "protobuf-c.o"
    subprocess.run([
        "emcc", "-Oz", "-pthread", "-c", str(source / "protobuf-c/protobuf-c.c"),
        "-o", str(obj),
    ], check=True)
    lib = prefix / "lib"
    lib.mkdir(exist_ok=True)
    subprocess.run(["emar", "rcs", str(lib / "libprotobuf-c.a"), str(obj)], check=True)
    headers = prefix / "include/protobuf-c"
    headers.mkdir(parents=True, exist_ok=True)
    (headers / "protobuf-c.h").write_bytes((source / "protobuf-c/protobuf-c.h").read_bytes())


def header_variant(patch, digest, fields):
    # Native bundles for different hosts may carry differently packaged copies.
    for variant in [patch] + patch.get("variants", []):
        if digest in (variant[name] for name in fields):
            return variant
    return None


def copy_headers(source, prefix, manifest):
    include = prefix / "include"
    patches = [manifest["boost_mpl_backport"]]
    patches += manifest["boost_numeric_backport"]["headers"]
    patches += manifest["boost_geometry_math"]["headers"]
    patched = {patch["path"]: patch for patch in patches}
    trees = [(name, name) for name in (
        "fast_float", "rapidjson", "curl",
        "boost", "protobuf-c", "libxml2")]
    trees += [("libxml2/libxml", "libxml")]
    for source_tree, target_tree in trees:
        tree = source / source_tree
        if source_tree.startswith("libxml2") or source_tree == "protobuf-c":
            # xmlversion.h describes compiled features. Never pair the native
            # configuration header with the Wasm library.
            tree = prefix / "include" / source_tree
        if not tree.is_dir():
            raise SystemExit(f"Missing source headers: {tree}")
        for src in tree.rglob("*"):
            if not src.is_file():
                continue
            relative = Path(target_tree) / src.relative_to(tree)
            dst = include / relative
            patch = patched.get(relative.as_posix())
            if patch:
                variant = header_variant(patch, hashlib.sha256(src.read_bytes()).hexdigest(),
                                         ("input_sha256", "output_sha256"))
                if not variant:
                    raise SystemExit(f"Unverified source header: {src}")
                if dst.exists() and hashlib.sha256(dst.read_bytes()).hexdigest() == variant["output_sha256"]:
                    continue
            elif dst.exists():
                src_stat, dst_stat = src.stat(), dst.stat()
                if src_stat.st_size == dst_stat.st_size:
                    if src_stat.st_mtime_ns == dst_stat.st_mtime_ns or src.read_bytes() == dst.read_bytes():
                        continue
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)


def apply_header(include, patch, replacements):
    header = include / patch["path"]
    data = header.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if header_variant(patch, digest, ("output_sha256",)):
        return
    variant = header_variant(patch, digest, ("input_sha256",))
    if not variant:
        raise SystemExit(f"Unverified dependency header {header}: {digest}; update the dependency adaptation explicitly")
    updated = data
    for old, new in replacements:
        if old not in updated:
            raise SystemExit(f"Unexpected header adaptation context: {header}")
        updated = updated.replace(old, new)
    if hashlib.sha256(updated).hexdigest() != variant["output_sha256"]:
        raise SystemExit(f"Unexpected header adaptation result: {header}")
    header.write_bytes(updated)


def prepare_headers(source, prefix, manifest):
    include = prefix / "include"
    copy_headers(source, prefix, manifest)
    # https://github.com/boostorg/mpl/pull/77
    apply_header(include, manifest["boost_mpl_backport"], [(
        b"#if BOOST_WORKAROUND(__EDG_VERSION__, <= 243)\n",
        b"#if BOOST_WORKAROUND(__EDG_VERSION__, <= 243) || __cplusplus >= 201103L\n")])
    # Boost numeric_conversion commit 50a1eae: these enum constants do not
    # need MPL's next/prior, which instantiate out-of-range enum values.
    for patch in manifest["boost_numeric_backport"]["headers"]:
        apply_header(include, patch, [
            (b'"boost/mpl/integral_c.hpp"', b'"boost/type_traits/integral_constant.hpp"'),
            (b"mpl::integral_c<", b"boost::integral_constant<"),
        ])

    for patch in manifest["boost_geometry_math"]["headers"]:
        apply_header(include, patch, [
            (old.encode(), new.encode()) for old, new in patch["replacements"]
        ])


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
        compiler = subprocess.run(["emcc", "--version"], stdout=subprocess.PIPE, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        raise SystemExit(version_error)
    if not re.search(r" " + re.escape(sdk_version) + r"([ -]|$)", compiler.stdout.partition("\n")[0]):
        raise SystemExit(version_error)

    channel = Path(__file__).with_name("rust-toolchain-version").read_text().strip()
    subprocess.run([
        "rustup", "toolchain", "install", channel, "--profile", "minimal",
        "--no-update", "--no-self-update",
    ], cwd=repo, check=True)
    subprocess.run(["rustup", "component", "add", "rust-src", "--toolchain", channel], check=True)
    subprocess.run(["rustup", "target", "add", "wasm32-unknown-emscripten", "--toolchain", channel], check=True)
    prefix = Path(os.environ.get("SEEKDB_WASM_DEPS", repo / "deps/wasm")).resolve()
    prefix.mkdir(parents=True, exist_ok=True)
    os.environ["EM_CACHE"] = str(prefix / "em-cache")
    dependencies = json.loads(Path(__file__).with_name("dependencies.json").read_text())
    build_openssl(prefix, dependencies["openssl"], args.jobs)
    build_libxml2(prefix, dependencies["libxml2"], args.jobs)
    build_protobuf_c(prefix, dependencies["protobuf_c"])
    prepare_headers(repo / "deps/3rd/usr/local/oceanbase/deps/devel/include", prefix, dependencies)

    # Bazel consumes this prepared thread/exception ABI cache read-only.
    with tempfile.TemporaryDirectory(prefix="sysroot.", dir=prefix) as temporary:
        probe = Path(temporary)
        (probe / "main.cpp").write_text("int main() { return 0; }\n")
        subprocess.run([
            "emcc", str(probe / "main.cpp"), "-Oz", "-pthread", "-fexceptions", "-msimd128",
            "-sUSE_ZLIB=1", "-sWASMFS", "-sASSERTIONS=2", "-sPROXY_TO_PTHREAD=1", "-sEXIT_RUNTIME=1",
            "-o", str(probe / "main.mjs"),
        ], check=True)


if __name__ == "__main__":
    main()
