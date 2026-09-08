#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
"""Select Wasm runtime sources from the authoritative Bazel inventory."""

import argparse
import json
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    repo = args.repo.resolve()
    sys.path.insert(0, str(repo / "tools/cmake"))
    from emit_bazel_source_inventory import _read_assignments

    inventory = repo / "src/oblib/oblib_source_inventory.bzl"
    groups = _read_assignments(inventory)["OBLIB_UNITY_GROUPS"]
    module = "src/oblib/lib:ob_malloc_object"
    sources = []
    for group in groups[module]:
        if group["generated_srcs"] or group["external_srcs"]:
            raise RuntimeError("Allocator inventory now requires generated/external inputs; add target generation first")
        for source in group["srcs"]:
            if not (repo / source).is_file():
                raise RuntimeError(f"Missing allocator source: {source}")
            if source not in sources:
                sources.append(source)
    parser_sources = _read_assignments(repo / "src/sql/sql_source_inventory.bzl")["SQL_PARSER_SOURCES"]
    charset_sources = [source for group in groups["src/oblib/lib:oblib_lib"]
                       if group["name"] == "oblib_lib_charset_0" for source in group["srcs"]]
    if not charset_sources:
        raise RuntimeError("Charset source inventory is missing")
    components = json.loads((repo / "tools/wasm/runtime-components.json").read_text())
    all_sources = {source for records in groups.values() for group in records for source in group["srcs"]}
    support = set(components["sources"])
    missing = support - all_sources
    if missing:
        raise RuntimeError(f"Runtime components are not in the Bazel inventory: {sorted(missing)}")
    requested_groups = set(components["groups"])
    for records in groups.values():
        for group in records:
            if group["name"] in requested_groups:
                support.update(group["srcs"])
                if group["generated_srcs"] or group["external_srcs"]:
                    raise RuntimeError(f"Runtime group requires generated/external inputs: {group['name']}")
    missing_groups = requested_groups - {group["name"] for records in groups.values() for group in records}
    if missing_groups:
        raise RuntimeError(f"Unknown runtime groups: {sorted(missing_groups)}")
    for prefix in components["source_prefixes"]:
        matches = {source for source in all_sources if source.startswith(prefix)}
        if not matches:
            raise RuntimeError(f"Empty runtime source prefix: {prefix}")
        support.update(matches)
    support.difference_update(sources)

    def cmake_path(source):
        path = str(repo / source)
        return '"' + path.replace('\\', '\\\\').replace('"', '\\"').replace('$', '\\$').replace(';', '\\;') + '"'

    args.output.write_text(
        "# Generated from Bazel OBLib and SQL source inventories; do not hand-edit.\n"
        + "set(SEEKDB_WASM_ALLOCATOR_SOURCES\n"
        + "".join("  " + cmake_path(source) + "\n" for source in sources)
        + ")\nset(SEEKDB_WASM_RUNTIME_SUPPORT_SOURCES\n"
        + "".join("  " + cmake_path(source) + "\n" for source in sorted(support))
        + ")\nset(SEEKDB_WASM_PARSER_SOURCES\n"
        + "".join("  " + cmake_path(source) + "\n" for source in parser_sources)
        + ")\nset(SEEKDB_WASM_CHARSET_SOURCES\n"
        + "".join("  " + cmake_path(source) + "\n" for source in charset_sources)
        + ")\n"
    )
    args.output.with_suffix(".json").write_text(json.dumps({
        "module": module,
        "source_inventory": str(inventory.relative_to(repo)),
        "sources": sources,
        "excluded_sources": [],
        "runtime_support_sources": sorted(support),
        "parser_source_inventory": "src/sql/sql_source_inventory.bzl",
        "parser_sources": parser_sources,
        "charset_sources": charset_sources,
    }, indent=2) + "\n")


if __name__ == "__main__":
    main()
