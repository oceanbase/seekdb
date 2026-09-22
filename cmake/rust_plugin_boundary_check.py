#!/usr/bin/env python3
"""Audit the resolved local Cargo dependency boundary of a public Rust plugin.

Not a sandbox: registry/native build scripts still require developer trust.
Binary export/dependency auditing and closed linking are separate build gates.
"""
import argparse
import json
import pathlib
import subprocess


def inside(path, root):
    return path == root or root in path.parents


def validate(metadata, plugin_root, sdk_root):
    root_id = metadata["resolve"]["root"]
    if root_id is None:
        raise ValueError("plugin Cargo manifest must define a root package")
    packages = {package["id"]: package for package in metadata["packages"]}
    nodes = {node["id"]: node for node in metadata["resolve"]["nodes"]}
    pending = [root_id]
    seen = set()
    while pending:
        package_id = pending.pop()
        if package_id in seen:
            continue
        seen.add(package_id)
        package = packages[package_id]
        manifest = pathlib.Path(package["manifest_path"]).resolve()
        if package["source"] is None and not (inside(manifest, plugin_root) or inside(manifest, sdk_root)):
            raise ValueError(f"local Cargo dependency escapes plugin/public SDK trees: {manifest}")
        pending.extend(dependency["pkg"] for dependency in nodes[package_id]["deps"])
    root = packages[root_id]
    if not inside(pathlib.Path(root["manifest_path"]).resolve(), plugin_root):
        raise ValueError("Cargo root package is outside the plugin tree")
    if not any("cdylib" in target["crate_types"] for target in root["targets"]):
        raise ValueError("Rust plugin must provide a cdylib target")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cargo", required=True)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--plugin-root", required=True)
    parser.add_argument("--sdk-root", required=True)
    args = parser.parse_args()
    metadata = subprocess.run([args.cargo, "metadata", "--format-version=1", "--offline",
                               "--manifest-path", args.manifest], capture_output=True, text=True)
    if metadata.returncode != 0:
        raise SystemExit("Cargo metadata failed before the plugin boundary audit:\n" + metadata.stderr)
    validate(json.loads(metadata.stdout), pathlib.Path(args.plugin_root).resolve(), pathlib.Path(args.sdk_root).resolve())
    print("seekdb Rust plugin Cargo boundary check passed")


if __name__ == "__main__":
    main()
