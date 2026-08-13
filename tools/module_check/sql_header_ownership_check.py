#!/usr/bin/env python3
"""Verify SQL's native public/closure/private header ownership."""

import argparse
import ast
import re
import subprocess
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
INVENTORY_NAMES = (
    "SQL_PUBLIC_HEADER_ROOTS",
    "SQL_INTERFACE_CLOSURE_HEADERS",
    "SQL_COMPOSITION_HEADERS",
    "SQL_PRIVATE_HEADERS",
)
EXPECTED_COMPOSITION_HEADERS = {
    "das/iter/ob_das_scan_iter.h",
    "das/iter/ob_das_text_retrieval_eval_node.h",
    "parser/fts_base.h",
    "parser/fts_parse.h",
}
RUNTIME_DEPS_NAME = "SQL_RUNTIME_NATIVE_DEPS"

# Foundation/external labels are unrestricted.  SQL source-module labels must
# stay on the declared downward seams: Query API, Data Plane/Storage, or Share.
ALLOWED_RUNTIME_SOURCE_DEP_PREFIXES = (
    "//src/data_plane:",
    "//src/oblib/",
    "//src/oblib:",
    "//src/query:",
    "//src/share:",
    "//src/storage:",
)

# SQL is the query implementation.  These composition/peer implementations
# must be reached through Query/Data-plane interfaces rather than included.
FORBIDDEN_INCLUDE_PREFIXES = (
    "observer/",
    "pl/",
    "plugin/",
    "rootserver/",
    "src/observer/",
    "src/pl/",
    "src/plugin/",
    "src/rootserver/",
)


def _parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("repo", nargs="?", default=None)
    parser.add_argument(
        "--emit-inventory",
        action="store_true",
        help="print the derived sql_header_inventory.bzl",
    )
    parser.add_argument(
        "--strict-deps",
        action="store_true",
        help="enforce SQL source and Bazel dependencies on downward seams",
    )
    return parser.parse_args()


def _relative_to(path, root):
    try:
        return path.relative_to(root)
    except ValueError:
        return None


def _tracked_files(repo, root, suffixes):
    result = subprocess.run(
        [
            "git",
            "ls-files",
            "--cached",
            "--others",
            "--exclude-standard",
            root.relative_to(repo).as_posix(),
        ],
        cwd=repo,
        check=True,
        universal_newlines=True,
        stdout=subprocess.PIPE,
    )
    tracked = {}
    for value in result.stdout.splitlines():
        path = repo / value
        if path.is_file() and path.suffix in suffixes:
            tracked[path.relative_to(root).as_posix()] = path
    return tracked


def _load_inventory(path):
    tree = ast.parse(path.read_text(), filename=str(path))
    values = {}
    for node in tree.body:
        if not isinstance(node, ast.Assign) or len(node.targets) != 1:
            continue
        target = node.targets[0]
        if isinstance(target, ast.Name) and target.id in INVENTORY_NAMES:
            values[target.id] = ast.literal_eval(node.value)
    missing = sorted(set(INVENTORY_NAMES) - set(values))
    if missing:
        raise ValueError("missing inventory assignments: %s" % missing)
    return values


def _load_runtime_deps(path):
    tree = ast.parse(path.read_text(), filename=str(path))
    for node in tree.body:
        if not isinstance(node, ast.Assign) or len(node.targets) != 1:
            continue
        target = node.targets[0]
        if isinstance(target, ast.Name) and target.id == RUNTIME_DEPS_NAME:
            return ast.literal_eval(node.value)
    raise ValueError("missing %s assignment" % RUNTIME_DEPS_NAME)


def _forbidden_bazel_artifacts(sql_root):
    violations = []
    target_pattern = re.compile(r'name\s*=\s*"_headers[^"\n]*"')
    for path in list(sql_root.rglob("BUILD.bazel")) + list(sql_root.rglob("*.bzl")):
        text = path.read_text(errors="ignore")
        if "ob_sql_static_migration" in text:
            violations.append("%s -> ob_sql_static_migration" % path.relative_to(sql_root))
        for match in target_pattern.finditer(text):
            violations.append(
                "%s -> %s" % (path.relative_to(sql_root), match.group(0))
            )
        if "//src/sql:_headers" in text:
            violations.append("%s -> //src/sql:_headers*" % path.relative_to(sql_root))
    return sorted(violations)


def _resolve_sql_header(include, source, sql_root, headers):
    candidates = []
    if include.startswith("sql/"):
        candidates.append(include[len("sql/") :])
    if include.startswith("src/sql/"):
        candidates.append(include[len("src/sql/") :])
    source_relative = _relative_to(source, sql_root)
    if source_relative is not None:
        candidates.append((source_relative.parent / include).as_posix())
    candidates.append(include)
    for candidate in candidates:
        normalized = str(Path(candidate))
        if normalized in headers:
            return normalized
    return None


def _derive_ownership(repo, sql_root, headers):
    public_roots = set()
    consumers = {}
    sql_edges = {header: set() for header in headers}
    source_files = _tracked_files(repo, repo / "src", SOURCE_SUFFIXES)

    for source in source_files.values():
        source_relative = _relative_to(source, sql_root)
        for include in INCLUDE_PATTERN.findall(source.read_text(errors="ignore")):
            header = _resolve_sql_header(include, source, sql_root, headers)
            if header is None:
                continue
            if source_relative is None:
                public_roots.add(header)
                consumers.setdefault(header, set()).add(
                    source.relative_to(repo).as_posix()
                )
            elif source_relative.as_posix() in headers:
                sql_edges[source_relative.as_posix()].add(header)

    reachable = set(public_roots)
    pending = list(public_roots)
    while pending:
        header = pending.pop()
        for dependency in sql_edges[header]:
            if dependency not in reachable:
                reachable.add(dependency)
                pending.append(dependency)

    composition_headers = set(EXPECTED_COMPOSITION_HEADERS)
    return {
        "SQL_PUBLIC_HEADER_ROOTS": public_roots - composition_headers,
        "SQL_INTERFACE_CLOSURE_HEADERS": (
            reachable - public_roots - composition_headers
        ),
        "SQL_COMPOSITION_HEADERS": composition_headers,
        "SQL_PRIVATE_HEADERS": set(headers) - reachable - composition_headers,
    }, consumers


def _direct_upper_includes(sql_root):
    violations = []
    for relative, source in _tracked_files(
        sql_root.parents[1], sql_root, SOURCE_SUFFIXES
    ).items():
        for include in INCLUDE_PATTERN.findall(source.read_text(errors="ignore")):
            if include.startswith(FORBIDDEN_INCLUDE_PREFIXES):
                violations.append("%s -> %s" % (relative, include))
    return sorted(violations)


def _emit_inventory(derived):
    print('"""Native public/private header ownership for the SQL module."""')
    print()
    for index, name in enumerate(INVENTORY_NAMES):
        print("%s = [" % name)
        for header in sorted(derived[name]):
            print('    "%s",' % header)
        print("]")
        if index + 1 < len(INVENTORY_NAMES):
            print()


def main():
    args = _parse_args()
    repo = (
        Path(args.repo).resolve()
        if args.repo is not None
        else Path(__file__).resolve().parents[2]
    )
    sql_root = repo / "src/sql"
    headers = _tracked_files(repo, sql_root, HEADER_SUFFIXES)
    derived, consumers = _derive_ownership(repo, sql_root, headers)

    if args.emit_inventory:
        _emit_inventory(derived)
        return 0

    inventory_path = sql_root / "sql_header_inventory.bzl"
    inventory = {
        name: set(values)
        for name, values in _load_inventory(inventory_path).items()
    }
    errors = []
    all_owned = set()
    for name in INVENTORY_NAMES:
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

    upper_includes = _direct_upper_includes(sql_root)
    if args.strict_deps and upper_includes:
        errors.append(
            "SQL directly includes upper-layer implementations: %s"
            % upper_includes[:10]
        )

    runtime_deps = _load_runtime_deps(sql_root / "sql_runtime_deps.bzl")
    forbidden_runtime_deps = sorted(
        dep
        for dep in runtime_deps
        if dep.startswith("//src/")
        and not dep.startswith(ALLOWED_RUNTIME_SOURCE_DEP_PREFIXES)
    )
    compatibility_runtime_deps = sorted(
        dep
        for dep in runtime_deps
        if (
            dep.startswith("//src/")
            and dep.partition(":")[2].startswith("_headers")
        )
        or "migration" in dep
    )
    forbidden_bazel_artifacts = _forbidden_bazel_artifacts(sql_root)
    if args.strict_deps and forbidden_runtime_deps:
        errors.append(
            "SQL runtime has non-downward source dependencies: %s"
            % forbidden_runtime_deps[:10]
        )
    if args.strict_deps and compatibility_runtime_deps:
        errors.append(
            "SQL runtime has compatibility dependency labels: %s"
            % compatibility_runtime_deps[:10]
        )
    if args.strict_deps and forbidden_bazel_artifacts:
        errors.append(
            "SQL contains forbidden Bazel compatibility artifacts: %s"
            % forbidden_bazel_artifacts[:10]
        )

    if errors:
        for error in errors:
            print("[FAIL] " + error, file=sys.stderr)
        return 1

    print(
        "sql header ownership: %d tracked, %d public roots, "
        "%d interface closure, %d composition, %d private"
        % (
            len(headers),
            len(derived["SQL_PUBLIC_HEADER_ROOTS"]),
            len(derived["SQL_INTERFACE_CLOSURE_HEADERS"]),
            len(derived["SQL_COMPOSITION_HEADERS"]),
            len(derived["SQL_PRIVATE_HEADERS"]),
        )
    )
    print("sql upper-layer include check: direct includes %d" % len(upper_includes))
    print(
        "sql Bazel dependency check: non-downward %d, compatibility labels %d, "
        "compatibility artifacts %d"
        % (
            len(forbidden_runtime_deps),
            len(compatibility_runtime_deps),
            len(forbidden_bazel_artifacts),
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
