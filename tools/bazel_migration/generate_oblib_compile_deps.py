#!/usr/bin/env python3
"""Generate exact OBLib owner dependencies for non-partitioned compile targets.

Large semantic Unity modules use per-group dependency maps.  This generator
covers the remaining source-bearing cc_library actions (small semantic
libraries, generated single-Unity targets, and compile probes).  It walks each
translation unit's repository include closure until it reaches OBLib, then
records the fine-grained owners of those directly reached OBLib headers.
"""

from collections import defaultdict
from pathlib import Path
import argparse
import re
import subprocess
import sys
import xml.etree.ElementTree as ET


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
COMPILE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx"}
PARTITIONED_TARGET_PREFIXES = (
    "//src/observer:_observer_main_",
    "//src/observer:_observer_retrieval_composition_",
    "//src/observer:_observer_runtime_",
    "//src/pl:_pl_parser_c_",
    "//src/pl:_pl_runtime_",
    "//src/rootserver:_rootserver_runtime_",
    "//src/share:_share_runtime_",
    "//src/sql:_sql_runtime_",
    "//src/storage:_storage_runtime_",
)
INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s*[<"]([^>"]+)[>"]',
    re.MULTILINE,
)


def _read(path):
    return path.read_text(encoding="utf-8", errors="surrogateescape")


def _query_xml(repo, expression):
    return ET.fromstring(
        subprocess.check_output(
            [
                str(repo / "bazel.py"),
                "query",
                expression,
                "--output=xml",
            ],
            cwd=str(repo),
        )
    )


def _load_owners(repo):
    namespace = {}
    inventory = repo / "src" / "oblib" / "oblib_header_inventory.bzl"
    exec(compile(_read(inventory), str(inventory), "exec"), {}, namespace)
    return {
        header: "//src/oblib" + owner
        for header, owner in namespace[
            "OBLIB_HEADER_TARGET_FOR_HEADER"
        ].items()
    }


def _source_index(repo):
    relative = {}
    source_root = repo / "src"
    for path in source_root.rglob("*"):
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES:
            relative[path.relative_to(repo).as_posix()] = path.resolve()
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


def _resolve_include(source, include, repo, index):
    relative, suffixes, basenames = index
    candidates = [
        (source.parent / include).resolve(),
        (repo / include).resolve(),
        (repo / "src" / include).resolve(),
        (repo / "src" / "oblib" / include).resolve(),
        (repo / "src" / "query" / "api" / include).resolve(),
        (repo / "src" / "data_plane" / "api" / include).resolve(),
    ]
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
    matches = basenames.get(Path(include).name, ())
    return matches[0] if len(matches) == 1 else None


def _oblib_header(path, repo):
    try:
        relative = path.relative_to(repo / "src" / "oblib").as_posix()
    except ValueError:
        return None
    return None if relative.startswith("easy/") else relative


def _list_labels(rule, attribute):
    value = rule.find("list[@name='%s']" % attribute)
    if value is None:
        return []
    return [label.attrib["value"] for label in value.findall("label")]


def _generated_unity_members(repo):
    rules = _query_xml(
        repo,
        'kind("_emit_unity_source rule", //src/...:*)',
    )
    result = {}
    for rule in rules.findall("rule"):
        result[rule.attrib["name"]] = _list_labels(rule, "srcs")
    return result


def _label_sources(label, repo, generated_unity_members, resolving=None):
    resolving = set() if resolving is None else resolving
    if label in resolving:
        return []
    if label in generated_unity_members:
        result = []
        for member in generated_unity_members[label]:
            result.extend(
                _label_sources(
                    member,
                    repo,
                    generated_unity_members,
                    resolving | {label},
                )
            )
        return result
    if not label.startswith("//") or ":" not in label:
        return []
    package, name = label[2:].split(":", 1)
    path = repo / package / name
    return [path.resolve()] if path.is_file() else []


def _component_closures(graph, direct_owners):
    indices = {}
    lowlinks = {}
    stack = []
    on_stack = set()
    components = []

    def visit(node):
        indices[node] = len(indices)
        lowlinks[node] = indices[node]
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

    for node in graph:
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

    cache = {}

    def closure(component):
        if component not in cache:
            result = set(component_owners[component])
            for dependency in component_graph[component]:
                result.update(closure(dependency))
            cache[component] = result
        return cache[component]

    for component in component_graph:
        closure(component)
    return component_by_path, cache


def _build_registry(repo):
    owners = _load_owners(repo)
    generated_unity_members = _generated_unity_members(repo)
    cc_rules = _query_xml(
        repo,
        'kind("cc_library rule", //src/...:*) except //src/oblib/...:*',
    )
    roots_by_target = {}
    all_roots = set()
    for rule in cc_rules.findall("rule"):
        roots = []
        for label in _list_labels(rule, "srcs"):
            roots.extend(
                _label_sources(label, repo, generated_unity_members)
            )
        roots = sorted(
            {
                path
                for path in roots
                if path.suffix.lower() in COMPILE_SUFFIXES
            }
        )
        if roots:
            target = rule.attrib["name"]
            if (
                target.endswith("_objects")
                and any(
                    target.startswith(prefix)
                    for prefix in PARTITIONED_TARGET_PREFIXES
                )
            ):
                continue
            roots_by_target[target] = roots
            all_roots.update(roots)

    index = _source_index(repo)
    graph = {}
    direct_owners = {}
    pending = list(all_roots)
    while pending:
        path = pending.pop()
        if path in graph:
            continue
        dependencies = set()
        path_owners = set()
        for include in INCLUDE_RE.findall(_read(path)):
            dependency = _resolve_include(path, include, repo, index)
            if dependency is None:
                continue
            header = _oblib_header(dependency, repo)
            if header is not None:
                owner = owners.get(header)
                if owner is None:
                    raise ValueError(
                        "%s reaches private OBLib header %s"
                        % (path, header)
                    )
                path_owners.add(owner)
            else:
                dependencies.add(dependency)
                pending.append(dependency)
        graph[path] = dependencies
        direct_owners[path] = path_owners

    component_by_path, closures = _component_closures(
        graph,
        direct_owners,
    )
    registry = {}
    for target, roots in sorted(roots_by_target.items()):
        if target.startswith("//src/oblib:"):
            continue
        required = {
            owner
            for root in roots
            for owner in closures[component_by_path[root]]
        }
        if required:
            registry[target] = sorted(required)
    return registry


def _render(registry):
    lines = [
        '"""Generated exact OBLib dependencies for compile actions."""',
        "",
        "_OBLIB_COMPILE_DEPS = {",
    ]
    for target, dependencies in registry.items():
        lines.append('    "%s": [' % target)
        for dependency in dependencies:
            lines.append('        "%s",' % dependency)
        lines.append("    ],")
    lines.extend(
        [
            "}",
            "",
            "def oblib_compile_deps(name):",
            '    label = "//%s:%s" % (native.package_name(), name)',
            "    return _OBLIB_COMPILE_DEPS.get(label, [])",
            "",
        ]
    )
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    output = arguments.output or repo / "bazel" / "oblib_compile_deps.bzl"
    rendered = _render(_build_registry(repo))
    if arguments.check:
        if not output.is_file() or _read(output) != rendered:
            print(
                "OBLib compile dependency registry is stale: %s" % output,
                file=sys.stderr,
            )
            return 1
        return 0
    output.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
