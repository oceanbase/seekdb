#!/usr/bin/env python3
"""Verify OBLib's terminal Bazel header and dependency architecture."""

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
UPPER_MODULES = {
    "data_plane",
    "logservice",
    "objit",
    "observer",
    "pl",
    "query",
    "rootserver",
    "share",
    "sql",
    "storage",
}
UPPER_INCLUDE = re.compile(
    r'^\s*#\s*include\s*[<"](?:src/)?('
    + "|".join(sorted(UPPER_MODULES))
    + r")(?:/|[>\"])",
    re.MULTILINE,
)
RUNTIME_LOCATOR = re.compile(
    r"\b(?:GCTX|GSCHEMASERVICE|g_mp|MTL(?:_[A-Z0-9_]+)?\s*\()"
)
FORBIDDEN_TARGET_NAME = re.compile(
    r"(?:_legacy_headers$|_headers_root$|_migration$|"
    r"^oblib_(?:all|full|public|runtime)_headers$|"
    r"^oblib_.*headers_(?:aggregate|closure)$)"
)
OBLIB_INTERNAL_LABEL = re.compile(
    r"//src/oblib:(?:_oblib_implementation_headers|_oblib_.*_impl)"
)
OBLIB_PUBLIC_LABEL = re.compile(r"//src/oblib:([A-Za-z0-9_+.-]+)")
SKIPPED_DIRS = {
    ".git",
    "__pycache__",
    "build",
    "build_bazel",
    "deps",
}


def _read(path):
    return path.read_text(encoding="utf-8", errors="surrogateescape")


def _fail(errors, message):
    errors.append(message)


def _walk_files(root, suffixes=None):
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        if any(part in SKIPPED_DIRS for part in path.parts):
            continue
        if suffixes is None or path.suffix.lower() in suffixes:
            yield path


def _load_inventory(repo):
    namespace = {}
    path = repo / "src" / "oblib" / "oblib_header_inventory.bzl"
    exec(compile(_read(path), str(path), "exec"), {}, namespace)
    return namespace


def _check_inventory(repo, errors):
    for generator_name in (
        "generate_oblib_header_inventory.py",
        "generate_oblib_compile_deps.py",
    ):
        generator = (
            repo / "tools" / "bazel_migration" / generator_name
        )
        completed = subprocess.run(
            [sys.executable, str(generator), "--check"],
            cwd=str(repo),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        if completed.returncode:
            _fail(errors, completed.stdout.strip())

    inventory = _load_inventory(repo)
    specs = inventory["OBLIB_HEADER_TARGETS"]
    owner_by_header = inventory["OBLIB_HEADER_TARGET_FOR_HEADER"]
    private_headers = set(inventory["OBLIB_PRIVATE_HEADERS"])
    oblib = repo / "src" / "oblib"
    physical_headers = {
        path.relative_to(oblib).as_posix()
        for path in _walk_files(oblib, HEADER_SUFFIXES)
        if not path.relative_to(oblib).as_posix().startswith("easy/")
    }

    declared_public = []
    for name, spec in specs.items():
        if FORBIDDEN_TARGET_NAME.search(name):
            _fail(errors, "forbidden OBLib compatibility target: %s" % name)
        declared_public.extend(spec["hdrs"])
        if len(spec["hdrs"]) > 50:
            _fail(
                errors,
                "OBLib owner %s is a broad header aggregate (%d headers)"
                % (name, len(spec["hdrs"])),
            )
        for dependency in spec["deps"]:
            if dependency.startswith(":"):
                target = dependency[1:]
                if target not in specs:
                    _fail(
                        errors,
                        "%s depends on unknown OBLib owner %s"
                        % (name, dependency),
                    )
            elif dependency.startswith("//src/oblib/easy:"):
                pass
            elif dependency.startswith("@seekdb_3rd_headers//:"):
                pass
            else:
                _fail(
                    errors,
                    "%s has non-foundational dependency %s"
                    % (name, dependency),
                )

    public_headers = set(declared_public)
    if len(public_headers) != len(declared_public):
        _fail(errors, "an OBLib public header has multiple owners")
    if set(owner_by_header) != public_headers:
        _fail(errors, "OBLib owner map and public target hdrs differ")
    for header, owner in owner_by_header.items():
        if owner != ":" + next(
            (
                name
                for name, spec in specs.items()
                if header in spec["hdrs"]
            ),
            "",
        ):
            _fail(errors, "wrong owner mapping for %s: %s" % (header, owner))
    if public_headers & private_headers:
        _fail(errors, "OBLib public and private header inventories overlap")
    if public_headers | private_headers != physical_headers:
        missing = sorted(
            physical_headers - public_headers - private_headers
        )
        stale = sorted(
            (public_headers | private_headers) - physical_headers
        )
        _fail(
            errors,
            "OBLib physical header inventory differs: missing=%s stale=%s"
            % (missing, stale),
        )

    graph = {
        name: {
            dependency[1:]
            for dependency in spec["deps"]
            if dependency.startswith(":")
        }
        for name, spec in specs.items()
    }
    visiting = set()
    visited = set()

    def visit(name, chain):
        if name in visiting:
            _fail(
                errors,
                "cycle in OBLib public owner DAG: %s"
                % " -> ".join(chain + [name]),
            )
            return
        if name in visited:
            return
        visiting.add(name)
        for dependency in graph[name]:
            visit(dependency, chain + [name])
        visiting.remove(name)
        visited.add(name)

    for name in graph:
        visit(name, [])


def _check_source_semantics(repo, errors):
    oblib = repo / "src" / "oblib"
    for path in _walk_files(
        oblib,
        HEADER_SUFFIXES | {".c", ".cc", ".cpp", ".cxx"},
    ):
        if "easy" in path.relative_to(oblib).parts:
            continue
        text = _read(path)
        match = UPPER_INCLUDE.search(text)
        if match:
            _fail(
                errors,
                "%s includes upper module %s"
                % (path.relative_to(repo), match.group(1)),
            )
        match = RUNTIME_LOCATOR.search(text)
        if match:
            _fail(
                errors,
                "%s uses runtime locator %s"
                % (path.relative_to(repo), match.group(0)),
            )


def _check_build_boundaries(repo, errors):
    oblib = repo / "src" / "oblib"
    for root_name in ("bazel", "src", "unittest"):
        root = repo / root_name
        for path in _walk_files(root):
            if path.name not in {"BUILD", "BUILD.bazel"} and path.suffix != ".bzl":
                continue
            try:
                path.relative_to(oblib)
                inside_oblib = True
            except ValueError:
                inside_oblib = False
            if inside_oblib:
                continue
            # The probe deliberately names private targets and expects Bazel
            # visibility analysis to reject them.
            if path == repo / "bazel" / "probes" / "BUILD.bazel":
                continue
            match = OBLIB_INTERNAL_LABEL.search(_read(path))
            if match:
                _fail(
                    errors,
                    "%s depends on private OBLib target %s"
                    % (path.relative_to(repo), match.group(0)),
                )

    policy = _read(repo / "bazel" / "architecture" / "module_policy.bzl")
    if len(re.findall(r'^\s*"oblib"\s*:\s*\[\s*\],', policy, re.MULTILINE)) < 2:
        _fail(
            errors,
            "central production and unittest policies must keep oblib dependency-free",
        )


def _check_generated_dependency_maps(repo, errors):
    """Reject generated Unity maps that name removed OBLib owners."""

    inventory = _load_inventory(repo)
    public_targets = set(inventory["OBLIB_HEADER_TARGETS"])
    for path in (repo / "src").rglob("*_group_oblib_deps.bzl"):
        for target in sorted(set(OBLIB_PUBLIC_LABEL.findall(_read(path)))):
            if target not in public_targets:
                _fail(
                    errors,
                    "%s depends on removed OBLib owner //src/oblib:%s"
                    % (path.relative_to(repo), target),
                )


def main():
    repo = (
        Path(sys.argv[1]).resolve()
        if len(sys.argv) > 1
        else Path(__file__).resolve().parents[2]
    )
    errors = []
    _check_inventory(repo, errors)
    _check_source_semantics(repo, errors)
    _check_build_boundaries(repo, errors)
    _check_generated_dependency_maps(repo, errors)
    if errors:
        for error in errors:
            print("OBLib terminal check: %s" % error, file=sys.stderr)
        return 1
    print(
        "OBLib terminal check passed: explicit public owner DAG, private "
        "implementation seam, and no upward runtime access"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
