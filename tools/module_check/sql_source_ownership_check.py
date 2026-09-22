#!/usr/bin/env python3
"""Verify that SQL's native inventory owns every checked-in source exactly once."""

import subprocess
import sys
from collections import Counter
from pathlib import Path
from typing import Dict, List, Set

# Reuse the non-executing, data-only inventory reader. Do not regex-scan whole
# assignment sections: reference lists are not additional compilation owners.
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "cmake"))
from emit_bazel_source_inventory import InventoryError, _read_assignments


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx"}
INVENTORY_NAMES = (
    "SQL_UNITY_GROUPS",
    "SQL_SIMD_UNITY_GROUPS",
    "SQL_STANDALONE_SOURCES",
    "SQL_EXTRA_SOURCES",
    "SQL_PARSER_SOURCES",
    "SQL_GIS_PLUGIN_ADAPTER_SOURCES",
    "SQL_EXTENSION_RUNTIME_SOURCES",
)
SEPARATELY_OWNED_SOURCES = {
    # Upstream checked-in prototype duplicates ob_dtl_mem_manager and is not
    # referenced by the production DTL runtime.
    "src/sql/dtl/ob_dtl_memory_manager.cpp",
    "src/sql/bazel_pilot/optimizer_private_header_probe.cpp",
    "src/sql/bazel_pilot/optimizer_public_interface_probe.cpp",
    "src/sql/bazel_pilot/prepare_public_interface_probe.cpp",
    "src/sql/bazel_pilot/sql_parser_driver_private_header_probe.cpp",
    "src/sql/bazel_pilot/sql_parser_driver_public_interface_probe.cpp",
    "src/sql/bazel_pilot/undeclared_resolver_header_probe.cpp",
}


def _workspace_sql_sources(repo: Path) -> Set[str]:
    output = subprocess.check_output(
        [
            "git",
            "ls-files",
            "--cached",
            "--others",
            "--exclude-standard",
            "src/sql",
        ],
        cwd=repo,
        universal_newlines=True,
    )
    return {
        path
        for path in output.splitlines()
        if Path(path).suffix in SOURCE_SUFFIXES and (repo / path).is_file()
    }


def _inventory_sections(data: dict) -> Dict[str, List[str]]:
    sections = {}
    for name in INVENTORY_NAMES:
        records = data.get(name)
        if not isinstance(records, list):
            raise InventoryError(f"{name} must be a list")
        if name in ("SQL_UNITY_GROUPS", "SQL_SIMD_UNITY_GROUPS"):
            if any(not isinstance(group, dict) or not isinstance(group.get("srcs"), list) for group in records):
                raise InventoryError(f"{name} must contain Unity groups")
            paths = [path for group in records for path in group["srcs"]]
        elif name in ("SQL_STANDALONE_SOURCES", "SQL_EXTRA_SOURCES"):
            if any(not isinstance(record, dict) or record.get("kind") != "source" for record in records):
                raise InventoryError(f"{name} must contain source records")
            paths = [record.get("path") for record in records]
        else:
            paths = records
        if any(not isinstance(path, str) or not path.startswith("src/sql/") or
               ".." in Path(path).parts or Path(path).suffix not in SOURCE_SUFFIXES for path in paths):
            raise InventoryError(f"{name} contains an invalid SQL source path")
        sections[name] = paths
    return sections


def check(repo: Path) -> List[str]:
    inventory = repo / "src/sql/sql_source_inventory.bzl"
    data = _read_assignments(inventory)
    sections = _inventory_sections(data)
    owned = [path for paths in sections.values() for path in paths]
    duplicates = sorted(path for path, count in Counter(owned).items() if count > 1)

    tracked = _workspace_sql_sources(repo)
    separate = tracked & SEPARATELY_OWNED_SOURCES
    expected_inventory = tracked - separate
    actual_inventory = set(owned)

    errors = []
    if duplicates:
        errors.append(f"duplicate inventory sources: {duplicates}")
    missing = sorted(expected_inventory - actual_inventory)
    stale = sorted(actual_inventory - expected_inventory)
    if missing:
        errors.append(f"unowned checked-in SQL sources: {missing}")
    if stale:
        errors.append(f"stale/non-production SQL inventory sources: {stale}")

    expected_counts = {
        "SQL_UNITY_GROUPS": 1115,
        "SQL_SIMD_UNITY_GROUPS": 3,
        "SQL_STANDALONE_SOURCES": 7,
        "SQL_EXTRA_SOURCES": 21,
        "SQL_PARSER_SOURCES": 15,
        "SQL_GIS_PLUGIN_ADAPTER_SOURCES": 5,
        "SQL_EXTENSION_RUNTIME_SOURCES": 4,
    }
    for name, expected in expected_counts.items():
        actual = len(sections[name])
        if actual != expected:
            errors.append(f"{name} has {actual} sources, expected {expected}")
    missing_separate = sorted(SEPARATELY_OWNED_SOURCES - tracked)
    if missing_separate:
        errors.append(f"stale separately-owned SQL sources: {missing_separate}")
    replacements = data.get("SQL_CORE_GIS_REPLACED_SOURCES")
    baseline = set(path for name in INVENTORY_NAMES[:4] for path in sections[name])
    if not isinstance(replacements, list) or any(not isinstance(path, str) for path in replacements):
        errors.append("SQL GIS replacements must be a string list")
    elif len(replacements) != 16 or len(set(replacements)) != len(replacements) or not set(replacements) <= baseline:
        errors.append("SQL GIS replacements must reference 16 distinct baseline owners")
    return errors


def main() -> int:
    repo = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    try:
        errors = check(repo)
    except (InventoryError, OSError) as error:
        errors = [str(error)]
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("sql source ownership: 1159 baseline + 9 conditional + 7 separate = 1175 workspace")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
