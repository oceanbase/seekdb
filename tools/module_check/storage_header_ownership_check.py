#!/usr/bin/env python3
"""Verify Storage's native public/closure/private header ownership."""

import ast
import re
import sys
from pathlib import Path


HEADER_SUFFIXES = {
    ".def",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".inc",
    ".inl",
    ".ipp",
    ".tcc",
}
SOURCE_SUFFIXES = HEADER_SUFFIXES | {".c", ".cc", ".cpp", ".cxx"}
INCLUDE_PATTERN = re.compile(
    r'^[ \t]*#[ \t]*include[ \t]*[<"]([^">]+)[">]',
    re.MULTILINE,
)
EXPECTED_COUNTS = {
    "STORAGE_PUBLIC_HEADER_ROOTS": 131,
    "STORAGE_INTERFACE_CLOSURE_HEADERS": 469,
    "STORAGE_PRIVATE_HEADERS": 194,
    "STORAGE_SEMANTIC_HEADERS": 6,
}
EXPECTED_SEMANTIC_HEADERS = {
    "blocksstable/ob_io_bench_controller.h",
    "ls/ob_i_ls_runtime_adapter.h",
    "memtable/ob_i_multi_source_data_unit.h",
    "meta_mem/ob_i_storage_meta_obj.h",
    "ob_tablet_autoincrement_service.h",
    "tablet/ob_tablet_autoincrement_state.h",
}


def _relative_to(path, root):
    try:
        return path.relative_to(root)
    except ValueError:
        return None


def _load_inventory(path):
    tree = ast.parse(path.read_text(), filename=str(path))
    values = {}
    for node in tree.body:
        if not isinstance(node, ast.Assign) or len(node.targets) != 1:
            continue
        target = node.targets[0]
        if isinstance(target, ast.Name) and target.id in EXPECTED_COUNTS:
            values[target.id] = ast.literal_eval(node.value)
    missing = sorted(set(EXPECTED_COUNTS) - set(values))
    if missing:
        raise ValueError("missing inventory assignments: %s" % missing)
    return values


def _tracked_storage_headers(storage_root):
    return {
        path.relative_to(storage_root).as_posix(): path
        for path in storage_root.rglob("*")
        if path.is_file() and path.suffix in HEADER_SUFFIXES
    }


def _resolve_storage_header(include, source, storage_root, headers):
    candidates = []
    if include.startswith("storage/"):
        candidates.append(include[len("storage/") :])
    if include.startswith("src/storage/"):
        candidates.append(include[len("src/storage/") :])
    source_relative = _relative_to(source, storage_root)
    if source_relative is not None:
        candidates.append((source_relative.parent / include).as_posix())
    candidates.append(include)
    for candidate in candidates:
        normalized = str(Path(candidate))
        if normalized in headers:
            return normalized
    return None


def _derive_ownership(repo, storage_root, headers):
    public_roots = set()
    consumers = {}
    storage_edges = {header: set() for header in headers}

    for source in (repo / "src").rglob("*"):
        if not source.is_file() or source.suffix not in SOURCE_SUFFIXES:
            continue
        source_relative = _relative_to(source, storage_root)
        for include in INCLUDE_PATTERN.findall(source.read_text(errors="ignore")):
            header = _resolve_storage_header(
                include, source, storage_root, headers
            )
            if header is None:
                continue
            if source_relative is None:
                public_roots.add(header)
                consumers.setdefault(header, set()).add(
                    source.relative_to(repo).as_posix()
                )
            elif source_relative.as_posix() in headers:
                storage_edges[source_relative.as_posix()].add(header)

    reachable = set(public_roots)
    pending = list(public_roots)
    while pending:
        header = pending.pop()
        for dependency in storage_edges[header]:
            if dependency not in reachable:
                reachable.add(dependency)
                pending.append(dependency)

    semantic_headers = set(EXPECTED_SEMANTIC_HEADERS)
    return {
        "STORAGE_PUBLIC_HEADER_ROOTS": public_roots - semantic_headers,
        "STORAGE_INTERFACE_CLOSURE_HEADERS": (
            reachable - public_roots - semantic_headers
        ),
        "STORAGE_PRIVATE_HEADERS": (
            set(headers) - reachable - semantic_headers
        ),
        "STORAGE_SEMANTIC_HEADERS": semantic_headers,
    }, consumers


def _direct_sql_includes(storage_root):
    violations = []
    for source in storage_root.rglob("*"):
        if not source.is_file() or source.suffix not in SOURCE_SUFFIXES:
            continue
        for include in INCLUDE_PATTERN.findall(source.read_text(errors="ignore")):
            if include.startswith("sql/") or include.startswith("src/sql/"):
                violations.append(
                    "%s -> %s"
                    % (source.relative_to(storage_root).as_posix(), include)
                )
    return sorted(violations)


def main():
    repo = (
        Path(sys.argv[1]).resolve()
        if len(sys.argv) > 1
        else Path(__file__).resolve().parents[2]
    )
    storage_root = repo / "src/storage"
    inventory_path = storage_root / "storage_header_inventory.bzl"
    inventory = {
        name: set(values)
        for name, values in _load_inventory(inventory_path).items()
    }
    headers = _tracked_storage_headers(storage_root)
    derived, consumers = _derive_ownership(repo, storage_root, headers)

    errors = []
    all_owned = set()
    for name, expected_count in EXPECTED_COUNTS.items():
        values = inventory[name]
        if len(values) != expected_count:
            errors.append(
                "%s has %d entries, expected %d"
                % (name, len(values), expected_count)
            )
        duplicate_owners = sorted(all_owned & values)
        if duplicate_owners:
            errors.append("headers with multiple owners: %s" % duplicate_owners)
        all_owned.update(values)

        missing = sorted(derived[name] - values)
        stale = sorted(values - derived[name])
        if missing:
            detail = []
            for header in missing[:10]:
                sites = sorted(consumers.get(header, ()))[:3]
                detail.append("%s <- %s" % (header, sites))
            errors.append("%s missing current headers: %s" % (name, detail))
        if stale:
            errors.append("%s contains stale headers: %s" % (name, stale[:10]))

    tracked_headers = set(headers)
    if all_owned != tracked_headers:
        errors.append(
            "ownership is not complete: missing=%s extra=%s"
            % (
                sorted(tracked_headers - all_owned)[:10],
                sorted(all_owned - tracked_headers)[:10],
            )
        )

    sql_includes = _direct_sql_includes(storage_root)
    if sql_includes:
        errors.append("Storage directly includes SQL headers: %s" % sql_includes[:10])

    if errors:
        for error in errors:
            print("[FAIL] " + error, file=sys.stderr)
        return 1

    print(
        "storage header ownership: %d tracked, %d public roots, "
        "%d interface closure, %d private, %d semantic"
        % (
            len(headers),
            len(derived["STORAGE_PUBLIC_HEADER_ROOTS"]),
            len(derived["STORAGE_INTERFACE_CLOSURE_HEADERS"]),
            len(derived["STORAGE_PRIVATE_HEADERS"]),
            len(derived["STORAGE_SEMANTIC_HEADERS"]),
        )
    )
    print("storage SQL include check: direct includes 0")
    return 0


if __name__ == "__main__":
    sys.exit(main())
