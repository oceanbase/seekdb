#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
"""Apply verified dependency adaptations to copied Wasm headers only."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def copy_headers(source, include, manifest, target_prefix=None):
    patches = [manifest["boost_mpl_backport"], manifest["s2_wasm_port"]]
    patches += manifest["boost_numeric_backport"]["headers"]
    patched = {patch["path"]: patch for patch in patches}
    trees = [(name, name) for name in (
        "fast_float", "s2", "absl", "rapidjson", "roaring", "vsag", "curl",
        "boost", "protobuf-c", "libxml2", "icu")]
    trees += [("libxml2/libxml", "libxml"), ("icu/common/unicode", "unicode"),
              ("icu/i18n/unicode", "unicode")]
    for source_tree, target_tree in trees:
        tree = source / source_tree
        if target_prefix and (source_tree.startswith("libxml2") or source_tree.startswith("icu")
                              or source_tree in ("s2", "absl", "protobuf-c", "vsag")):
            # xmlversion.h describes compiled features. Never pair the native
            # configuration header with the Wasm library.
            tree = target_prefix / "include" / source_tree
        if not tree.is_dir():
            raise SystemExit(f"Missing source headers: {tree}")
        for src in tree.rglob("*"):
            if not src.is_file():
                continue
            relative = Path(target_tree) / src.relative_to(tree)
            dst = include / relative
            patch = patched.get(relative.as_posix())
            if patch:
                if hashlib.sha256(src.read_bytes()).hexdigest() not in (
                        patch["input_sha256"], patch["output_sha256"]):
                    raise SystemExit(f"Unverified source header: {src}")
                if dst.exists() and hashlib.sha256(dst.read_bytes()).hexdigest() == patch["output_sha256"]:
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
    if digest == patch["output_sha256"]:
        return
    if digest != patch["input_sha256"]:
        raise SystemExit(f"Unverified dependency header {header}: {digest}; update the dependency adaptation explicitly")
    updated = data
    for old, new in replacements:
        if old not in updated:
            raise SystemExit(f"Unexpected header adaptation context: {header}")
        updated = updated.replace(old, new)
    if hashlib.sha256(updated).hexdigest() != patch["output_sha256"]:
        raise SystemExit(f"Unexpected header adaptation result: {header}")
    header.write_bytes(updated)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--include", type=Path, required=True)
    parser.add_argument("--source", type=Path,
                        help="Copy selected dependency headers without touching unchanged patched files")
    parser.add_argument("--target-prefix", type=Path,
                        help="Use installed target headers for compiled dependencies")
    args = parser.parse_args()
    manifest = json.loads(Path(__file__).with_name("dependencies.json").read_text())
    if args.source:
        copy_headers(args.source, args.include, manifest, args.target_prefix)
    # https://github.com/boostorg/mpl/pull/77
    apply_header(args.include, manifest["boost_mpl_backport"], [(
        b"#if BOOST_WORKAROUND(__EDG_VERSION__, <= 243)\n",
        b"#if BOOST_WORKAROUND(__EDG_VERSION__, <= 243) || __cplusplus >= 201103L\n")])
    # Boost numeric_conversion commit 50a1eae: these enum constants do not
    # need MPL's next/prior, which instantiate out-of-range enum values.
    for patch in manifest["boost_numeric_backport"]["headers"]:
        apply_header(args.include, patch, [
            (b'"boost/mpl/integral_c.hpp"', b'"boost/type_traits/integral_constant.hpp"'),
            (b"mpl::integral_c<", b"boost::integral_constant<"),
        ])

    # S2's fallback conflicts with musl when another header included byteswap.h.
    apply_header(args.include, manifest["s2_wasm_port"], [(
        b"#elif defined(__GLIBC__) || defined(__BIONIC__) || defined(__ASYLO__) || ",
        b"#elif defined(__EMSCRIPTEN__) || defined(__GLIBC__) || defined(__BIONIC__) || defined(__ASYLO__) || ")])


if __name__ == "__main__":
    main()
