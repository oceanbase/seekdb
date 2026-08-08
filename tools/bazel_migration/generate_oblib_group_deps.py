#!/usr/bin/env python3
"""Generate exact OBLib header-owner dependencies for Bazel Unity groups.

Run this after Bazel has analyzed or built the semantic Unity target.  The
generated Unity sources retain the checked-in member list, which this tool
walks through the repository include graph until it reaches OBLib's public
header seam.
"""

from collections import defaultdict
from pathlib import Path
import argparse
import re
import subprocess
import sys


SOURCE_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
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
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)


def _read(path):
    return path.read_text(encoding="utf-8", errors="surrogateescape")


def _load_owners(repo):
    namespace = {}
    inventory = repo / "src" / "oblib" / "oblib_header_inventory.bzl"
    exec(compile(_read(inventory), str(inventory), "exec"), {}, namespace)
    return {
        header: "//src/oblib" + target
        for header, target in namespace[
            "OBLIB_HEADER_TARGET_FOR_HEADER"
        ].items()
    }


def _source_index(repo):
    relative = {}
    source_root = repo / "src"
    for path in source_root.rglob("*"):
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES:
            relative[path.relative_to(repo).as_posix()] = path
    generated_root = repo / "build_bazel" / "bin" / "src"
    if generated_root.exists():
        for path in generated_root.rglob("*"):
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES:
                relative[
                    "src/" + path.relative_to(generated_root).as_posix()
                ] = path
    suffixes = defaultdict(list)
    basenames = defaultdict(list)
    for name, path in relative.items():
        basenames[path.name].append(path)
        if name.startswith("src/"):
            suffixes[name[len("src/") :]].append(path)
        for marker in ("src/query/api/", "src/data_plane/api/"):
            if name.startswith(marker):
                suffixes[name[len(marker) :]].append(path)
    return relative, suffixes, basenames


def _resolve_include(source, include, repo, relative, suffixes, basenames):
    candidates = []
    if source is not None:
        candidates.append((source.parent / include).resolve())
    candidates.extend(
        [
            (repo / include).resolve(),
            (repo / "src" / include).resolve(),
            (repo / "src" / "oblib" / include).resolve(),
            (repo / "src" / "query" / "api" / include).resolve(),
            (repo / "src" / "data_plane" / "api" / include).resolve(),
        ]
    )
    for candidate in candidates:
        try:
            name = candidate.relative_to(repo).as_posix()
        except ValueError:
            continue
        if name in relative:
            return relative[name]
    matches = suffixes.get(include, ())
    if len(matches) == 1:
        return matches[0]
    resolved_matches = {path.resolve() for path in matches}
    if len(resolved_matches) == 1:
        return next(iter(resolved_matches))
    matches = basenames.get(Path(include).name, ())
    if len(matches) == 1:
        return matches[0]
    resolved_matches = {path.resolve() for path in matches}
    return next(iter(resolved_matches)) if len(resolved_matches) == 1 else None


def _oblib_header(path, repo):
    oblib = repo / "src" / "oblib"
    try:
        relative = path.relative_to(oblib).as_posix()
    except ValueError:
        return None
    return None if relative.startswith("easy/") else relative


def _all_group_deps(unity_sources, target_prefix, repo, owner_by_header, index):
    relative, suffixes, basenames = index
    group_roots = {}
    pending = []
    prefix = target_prefix + "_"
    for unity_source in unity_sources:
        name = unity_source.name
        suffix = ".unity.cpp" if name.endswith(".unity.cpp") else ".unity.c"
        group = name[len(prefix) : -len(suffix)]
        roots = []
        for include in INCLUDE_RE.findall(_read(unity_source)):
            resolved = _resolve_include(
                unity_source, include, repo, relative, suffixes, basenames
            )
            if resolved is None:
                raise ValueError(
                    "%s: cannot resolve Unity member %s"
                    % (unity_source, include)
                )
            roots.append(resolved)
        group_roots[group] = roots
        pending.extend(roots)

    graph = {}
    direct_owners = {}
    visited = set()
    while pending:
        path = pending.pop()
        if path in visited:
            continue
        visited.add(path)
        header = _oblib_header(path, repo)
        if header is not None:
            continue
        dependencies = set()
        owners = set()
        for include in INCLUDE_RE.findall(_read(path)):
            dependency = _resolve_include(
                path, include, repo, relative, suffixes, basenames
            )
            if dependency is None:
                continue
            oblib_header = _oblib_header(dependency, repo)
            if oblib_header is not None:
                owner = owner_by_header.get(oblib_header)
                if owner is None:
                    raise ValueError(
                        "%s reaches private OBLib header %s"
                        % (path, oblib_header)
                    )
                owners.add(owner)
            else:
                dependencies.add(dependency)
                pending.append(dependency)
        graph[path] = dependencies
        direct_owners[path] = owners

    # The source include graph contains legitimate same-module cycles. Collapse
    # them once, then propagate OBLib header owners over the resulting DAG.
    indices = {}
    lowlinks = {}
    stack = []
    on_stack = set()
    components = []
    next_index = [0]

    def visit(node):
        indices[node] = next_index[0]
        lowlinks[node] = next_index[0]
        next_index[0] += 1
        stack.append(node)
        on_stack.add(node)
        for dependency in graph.get(node, ()):
            if dependency not in indices:
                visit(dependency)
                lowlinks[node] = min(lowlinks[node], lowlinks[dependency])
            elif dependency in on_stack:
                lowlinks[node] = min(lowlinks[node], indices[dependency])
        if lowlinks[node] == indices[node]:
            component = []
            while True:
                member = stack.pop()
                on_stack.remove(member)
                component.append(member)
                if member == node:
                    break
            components.append(component)

    for node in sorted(graph, key=str):
        if node not in indices:
            visit(node)

    component_by_path = {
        path: index
        for index, component in enumerate(components)
        for path in component
    }
    component_graph = {index: set() for index in range(len(components))}
    component_owners = {index: set() for index in range(len(components))}
    for path, dependencies in graph.items():
        owner = component_by_path[path]
        component_owners[owner].update(direct_owners[path])
        for dependency in dependencies:
            dependency_owner = component_by_path[dependency]
            if owner != dependency_owner:
                component_graph[owner].add(dependency_owner)

    closure_cache = {}

    def closure(component):
        if component not in closure_cache:
            result = set(component_owners[component])
            for dependency in component_graph[component]:
                result.update(closure(dependency))
            closure_cache[component] = result
        return closure_cache[component]

    result = {}
    for group, roots in group_roots.items():
        owners = set()
        for root in roots:
            header = _oblib_header(root, repo)
            if header is not None:
                owner = owner_by_header.get(header)
                if owner is None:
                    raise ValueError(
                        "%s reaches private OBLib header %s" % (group, header)
                    )
                owners.add(owner)
            else:
                owners.update(closure(component_by_path[root]))
        result[group] = sorted(owners)
    return result


def _render(symbol, group_deps):
    private_symbol = "_" + symbol.upper()
    lines = [
        '"""Generated exact OBLib dependencies for semantic Unity groups."""',
        "",
        "%s = {" % private_symbol,
    ]
    for group in sorted(group_deps):
        lines.append('    "%s": [' % group)
        for dependency in group_deps[group]:
            lines.append('        "%s",' % dependency)
        lines.append("    ],")
    lines.extend(
        [
            "}",
            "",
            "def %s(groups, base = {}):" % symbol,
            "    actual = {group.name: True for group in groups}",
            "    unknown = sorted([",
            "        name",
            "        for name in %s" % private_symbol,
            "        if name not in actual",
            "    ])",
            "    missing = sorted([",
            "        name",
            "        for name in actual",
            "        if name not in %s" % private_symbol,
            "    ])",
            "    if unknown or missing:",
            '        fail("OBLib Unity dependency map differs from inventory: unknown=%s missing=%s" % (unknown, missing))',
            "    return {",
            "        name: base.get(name, []) + %s[name]" % private_symbol,
            "        for name in actual",
            "    }",
            "",
        ]
    )
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--unity-dir", type=Path, required=True)
    parser.add_argument("--target", required=True)
    parser.add_argument("--target-prefix", required=True)
    parser.add_argument("--symbol", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--groups-file",
        type=Path,
        help="take the current group names from an existing generated map",
    )
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()

    repo = Path(__file__).resolve().parents[2]
    unity_dir = arguments.unity_dir.resolve()
    if arguments.groups_file:
        current_groups = set(
            re.findall(
                r'^    "([^"]+)": \[$',
                _read(arguments.groups_file),
                re.MULTILINE,
            )
        )
    else:
        package = arguments.target.split(":", 1)[0]
        query = 'filter("^%s:_%s_.*_generated_unity$", deps(%s))' % (
            package,
            arguments.target_prefix,
            arguments.target,
        )
        labels = subprocess.check_output(
            [str(repo / "bazel.py"), "query", query, "--output=label"],
            cwd=str(repo),
            universal_newlines=True,
        )
        label_prefix = package + ":_" + arguments.target_prefix + "_"
        current_groups = {
            label[len(label_prefix) : -len("_generated_unity")]
            for label in labels.splitlines()
            if label.startswith(label_prefix)
            and label.endswith("_generated_unity")
        }

    patterns = (
        arguments.target_prefix + "_*.unity.cpp",
        arguments.target_prefix + "_*.unity.c",
    )
    unity_sources = sorted(
        {
            path
            for pattern in patterns
            for path in unity_dir.glob(pattern)
            if path.is_file()
        }
    )
    unity_sources = [
        path
        for path in unity_sources
        if any(
            path.name
            == arguments.target_prefix
            + "_"
            + group
            + suffix
            for group in current_groups
            for suffix in (".unity.cpp", ".unity.c")
        )
    ]
    if not unity_sources:
        print(
            "no generated Unity sources found under %s for %s"
            % (unity_dir, arguments.target_prefix),
            file=sys.stderr,
        )
        return 1

    owner_by_header = _load_owners(repo)
    index = _source_index(repo)
    group_deps = _all_group_deps(
        unity_sources,
        arguments.target_prefix,
        repo,
        owner_by_header,
        index,
    )
    missing_groups = sorted(current_groups - set(group_deps))
    if missing_groups:
        print(
            "generated Unity source is missing for groups: %s"
            % ", ".join(missing_groups),
            file=sys.stderr,
        )
        return 1
    rendered = _render(arguments.symbol, group_deps)
    if arguments.check:
        if not arguments.output.is_file() or _read(arguments.output) != rendered:
            print(
                "OBLib Unity dependency map is stale: %s" % arguments.output,
                file=sys.stderr,
            )
            return 1
    else:
        arguments.output.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
