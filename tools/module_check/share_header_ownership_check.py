#!/usr/bin/env python3
"""Verify Share's native public/closure/private header ownership."""

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
INVENTORY_NAMES = {
    "SHARE_PUBLIC_HEADER_ROOTS",
    "SHARE_INTERFACE_CLOSURE_HEADERS",
    "SHARE_PRIVATE_HEADERS",
}
GENERATED_SOURCE_TREE_HEADERS = {
    "inner_table/ob_inner_table_schema.h",
    "inner_table/ob_inner_table_schema_constants.h",
    "inner_table/ob_inner_table_schema_misc.ipp",
}
DECLARED_PUBLIC_HEADER_ROOTS = {
    "cache/ob_cache_name_define.h",
    "compaction/ob_ckm_error_tablet_info.h",
    "ob_ddl_task_serialize_field.h",
    "ob_id_generator.h",
    "schema/ob_schema_publish_signal.h",
    "table/ob_ttl_schedule.h",
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
        if isinstance(target, ast.Name) and target.id in INVENTORY_NAMES:
            values[target.id] = ast.literal_eval(node.value)
    missing = sorted(INVENTORY_NAMES - set(values))
    if missing:
        raise ValueError("missing inventory assignments: %s" % missing)
    return values


def _tracked_share_headers(repo, share_root):
    return {
        path.relative_to(share_root).as_posix(): path
        for path in share_root.rglob("*")
        if path.is_file()
        and path.suffix in HEADER_SUFFIXES
        and path.relative_to(share_root).as_posix() not in GENERATED_SOURCE_TREE_HEADERS
    }


def _resolve_share_header(include, source, share_root, headers):
    candidates = []
    if include.startswith("share/"):
        candidates.append(include[len("share/") :])
    if include.startswith("src/share/"):
        candidates.append(include[len("src/share/") :])
    source_relative = _relative_to(source, share_root)
    if source_relative is not None:
        candidates.append((source_relative.parent / include).as_posix())
    candidates.append(include)
    for candidate in candidates:
        normalized = str(Path(candidate))
        if normalized in headers:
            return normalized
    return None


def _derive_ownership(repo, share_root, headers):
    public_roots = set()
    consumers = {}
    share_edges = {header: set() for header in headers}

    for source in (repo / "src").rglob("*"):
        if not source.is_file() or source.suffix not in SOURCE_SUFFIXES:
            continue
        source_relative = _relative_to(source, share_root)
        for include in INCLUDE_PATTERN.findall(source.read_text(errors="ignore")):
            header = _resolve_share_header(include, source, share_root, headers)
            if header is None:
                continue
            if source_relative is None:
                public_roots.add(header)
                consumers.setdefault(header, set()).add(
                    source.relative_to(repo).as_posix()
                )
            elif source_relative.as_posix() in headers:
                share_edges[source_relative.as_posix()].add(header)

    public_roots.update(DECLARED_PUBLIC_HEADER_ROOTS & set(headers))
    reachable = set(public_roots)
    pending = list(public_roots)
    while pending:
        header = pending.pop()
        for dependency in share_edges[header]:
            if dependency not in reachable:
                reachable.add(dependency)
                pending.append(dependency)

    return {
        "SHARE_PUBLIC_HEADER_ROOTS": public_roots,
        "SHARE_INTERFACE_CLOSURE_HEADERS": reachable - public_roots,
        "SHARE_PRIVATE_HEADERS": set(headers) - reachable,
    }, consumers


def main():
    repo = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
    share_root = repo / "src/share"
    inventory_path = share_root / "share_header_inventory.bzl"
    inventory = {
        name: set(values)
        for name, values in _load_inventory(inventory_path).items()
    }
    headers = _tracked_share_headers(repo, share_root)
    derived, consumers = _derive_ownership(repo, share_root, headers)

    errors = []
    all_owned = set()
    for name in sorted(INVENTORY_NAMES):
        values = inventory[name]
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

    if errors:
        for error in errors:
            print("[FAIL] " + error, file=sys.stderr)
        return 1

    print(
        "share header ownership: %d tracked, %d public roots, "
        "%d interface closure, %d private"
        % (
            len(headers),
            len(derived["SHARE_PUBLIC_HEADER_ROOTS"]),
            len(derived["SHARE_INTERFACE_CLOSURE_HEADERS"]),
            len(derived["SHARE_PRIVATE_HEADERS"]),
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
