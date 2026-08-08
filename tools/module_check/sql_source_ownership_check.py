#!/usr/bin/env python3
"""Verify that SQL's native inventory owns every checked-in source exactly once."""

import re
import subprocess
import sys
from collections import Counter
from pathlib import Path
from typing import Dict, List, Set


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx"}
INVENTORY_NAMES = (
    "SQL_UNITY_GROUPS",
    "SQL_SIMD_UNITY_GROUPS",
    "SQL_STANDALONE_SOURCES",
    "SQL_EXTRA_SOURCES",
    "SQL_PARSER_SOURCES",
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


def _inventory_sections(text: str) -> Dict[str, List[str]]:
    starts = {
        name: text.index(f"{name} = [")
        for name in INVENTORY_NAMES
    }
    sections = {}  # type: Dict[str, List[str]]
    for name, start in starts.items():
        end = min(
            [value for value in starts.values() if value > start],
            default=len(text),
        )
        sections[name] = re.findall(
            r'"(src/sql/[^"\n]+\.(?:c|cc|cpp|cxx))"',
            text[start:end],
        )
    return sections


def check(repo: Path) -> List[str]:
    inventory = repo / "src/sql/sql_source_inventory.bzl"
    sections = _inventory_sections(inventory.read_text(encoding="utf-8"))
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
        "SQL_UNITY_GROUPS": 1107,
        "SQL_SIMD_UNITY_GROUPS": 3,
        "SQL_STANDALONE_SOURCES": 5,
        "SQL_EXTRA_SOURCES": 21,
        "SQL_PARSER_SOURCES": 15,
    }
    for name, expected in expected_counts.items():
        actual = len(sections[name])
        if actual != expected:
            errors.append(f"{name} has {actual} sources, expected {expected}")
    missing_separate = sorted(SEPARATELY_OWNED_SOURCES - tracked)
    if missing_separate:
        errors.append(f"stale separately-owned SQL sources: {missing_separate}")
    return errors


def main() -> int:
    repo = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    errors = check(repo)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("sql source ownership: 1151 production + 7 separate = 1158 workspace")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
