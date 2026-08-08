#!/usr/bin/env python3
"""Generate OBLib's explicit semantic header ownership inventory.

Every externally reached header belongs to one public semantic owner.  Owners
form an acyclic graph derived from the real header include graph; there is no
public all-headers facade.  Headers not reached by production modules or their
module-owned tests stay in OBLib's private implementation seam.  OBLib's own
tests deliberately do not make an implementation header public.
"""

from collections import defaultdict
from pathlib import Path
import argparse
import re
import sys


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
SOURCE_MODULES = (
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
)
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)

MEMORY_FAMILIES = {
    "alloc",
    "allocator",
    "core_local",
    "lds",
    "objectpool",
    "resource",
}
CONCURRENCY_FAMILIES = {
    "atomic",
    "coro",
    "future",
    "guard",
    "list",
    "lock",
    "queue",
    "rc",
    "task",
    "thread",
    "thread_local",
}
COLLECTION_FAMILIES = {"container", "hash", "hash_func", "string"}
DIAGNOSTIC_FAMILIES = {
    "errsim_module",
    "metrics",
    "oblog",
    "profile",
    "signal",
    "stat",
    "statistic_event",
    "trace",
    "wait_event",
}
ENCODING_FAMILIES = {
    "charset",
    "checksum",
    "codec",
    "encode",
    "encrypt",
    "json",
    "vtoa",
}
IO_FAMILIES = {"file", "net", "ssl"}

TIER_NAMES = {
    0: "primitives",
    1: "base",
    6: "support",
    11: "runtime_base",
    16: "utilities",
    21: "runtime",
    26: "advanced",
    31: "services",
    36: "integration",
    41: "model",
    46: "domain",
}

THIRD_PARTY_INCLUDE_PREFIXES = (
    ("aio/", "@seekdb_3rd_headers//:libaio_headers"),
    ("curl/", "@seekdb_3rd_headers//:curl_headers"),
    ("libxml/", "@seekdb_3rd_headers//:libxml2_headers"),
    ("libxml2/", "@seekdb_3rd_headers//:libxml2_headers"),
    ("mysql/", "@seekdb_3rd_headers//:mariadb_headers"),
    ("openssl/", "@seekdb_3rd_headers//:openssl_headers"),
    ("rapidjson/", "@seekdb_3rd_headers//:rapidjson_headers"),
    ("vsag/", "@seekdb_3rd_headers//:vsag_headers"),
)
THIRD_PARTY_EXACT_INCLUDES = {
    "libaio.h": "@seekdb_3rd_headers//:libaio_headers",
    "mysql.h": "@seekdb_3rd_headers//:mariadb_headers",
    "zconf.h": "@seekdb_3rd_headers//:zlib_headers",
    "zlib.h": "@seekdb_3rd_headers//:zlib_headers",
}
ALL_THIRD_PARTY_DEPS = sorted(
    set(THIRD_PARTY_EXACT_INCLUDES.values())
    | {label for _, label in THIRD_PARTY_INCLUDE_PREFIXES}
)


def _read(path):
    return path.read_text(encoding="utf-8", errors="surrogateescape")


def _header_category(header):
    parts = header.split("/")
    if parts[0] == "rpc":
        return (
            "mysql_protocol"
            if len(parts) > 1 and parts[1] == "obmysql"
            else "rpc_transport"
        )
    if parts[0] == "common":
        family = parts[1] if len(parts) > 2 else "core"
        if family == "mysqlclient":
            return "mysql_client"
        if family in {"json_type", "udt", "xml"}:
            return "data_formats"
        if family in {"expression", "meta_programming"}:
            return "db_meta"
        if family == "log":
            return "db_log"
        if header == "common/sql_mode/ob_sql_mode.h":
            return "sql_mode"
        return "db_values"

    family = parts[1] if len(parts) > 2 else "core"
    if family == "compress" or header == "lib/oblog/ob_log_compressor.h":
        return "compression"
    if family == "restore":
        return "restore"
    if family == "vector":
        return "vector"
    if family in MEMORY_FAMILIES:
        return "memory"
    if family in CONCURRENCY_FAMILIES:
        return "concurrency"
    if family in COLLECTION_FAMILIES:
        return "collections"
    if family in DIAGNOSTIC_FAMILIES:
        return "diagnostics"
    if family in ENCODING_FAMILIES:
        return "encoding"
    if family in IO_FAMILIES:
        return "io"
    return "core"


def _tarjan(graph):
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
        for dependency in graph[node]:
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
            components.append(tuple(sorted(component)))

    for node in sorted(graph):
        if node not in indices:
            visit(node)
    return components


def _quote_list(values, indent="        "):
    if not values:
        return "[]"
    return "[\n%s\n    ]" % "\n".join(
        '%s"%s",' % (indent, value) for value in values
    )


def _resolve_include(source, include, headers, by_basename):
    stripped = (
        include[len("src/oblib/") :]
        if include.startswith("src/oblib/")
        else include
    )
    candidates = [include, stripped]
    if source:
        candidates.append((Path(source).parent / include).as_posix())
    for candidate in candidates:
        if candidate in headers:
            return candidate
    matches = by_basename.get(Path(include).name, ())
    return matches[0] if len(matches) == 1 else None


def _third_party_dep(include):
    if include in THIRD_PARTY_EXACT_INCLUDES:
        return THIRD_PARTY_EXACT_INCLUDES[include]
    for prefix, label in THIRD_PARTY_INCLUDE_PREFIXES:
        if include.startswith(prefix):
            return label
    return None


def _collect_graph(repo):
    oblib = repo / "src" / "oblib"
    all_headers = {
        path.relative_to(oblib).as_posix(): path
        for path in oblib.rglob("*")
        if path.is_file() and path.suffix.lower() in HEADER_SUFFIXES
    }
    headers = {
        name: path
        for name, path in all_headers.items()
        if not name.startswith("easy/")
    }
    by_basename = defaultdict(list)
    for header in all_headers:
        by_basename[Path(header).name].append(header)

    graph = {header: set() for header in headers}
    easy_deps = {header: False for header in headers}
    third_party_deps = {header: set() for header in headers}
    for header, path in headers.items():
        for include in INCLUDE_RE.findall(_read(path)):
            dependency = _resolve_include(
                header, include, all_headers, by_basename
            )
            if dependency in headers:
                graph[header].add(dependency)
            elif dependency and dependency.startswith("easy/"):
                easy_deps[header] = True
            else:
                third_party = _third_party_dep(include)
                if third_party:
                    third_party_deps[header].add(third_party)

    consumers = {header: set() for header in headers}
    for module in SOURCE_MODULES:
        roots = [repo / "src" / module, repo / "unittest" / module]
        for root in roots:
            if not root.exists():
                continue
            for path in root.rglob("*"):
                if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                    continue
                for include in INCLUDE_RE.findall(_read(path)):
                    dependency = _resolve_include(
                        "", include, all_headers, by_basename
                    )
                    if dependency in consumers:
                        consumers[dependency].add(module)

    changed = True
    while changed:
        changed = False
        for header, dependencies in graph.items():
            for dependency in dependencies:
                old_size = len(consumers[dependency])
                consumers[dependency].update(consumers[header])
                changed = changed or len(consumers[dependency]) != old_size

    return graph, consumers, easy_deps, third_party_deps


def _build_inventory(graph, consumers, easy_deps, third_party_deps):
    components = _tarjan(graph)
    component_by_header = {
        header: index
        for index, component in enumerate(components)
        for header in component
    }
    component_graph = {index: set() for index in range(len(components))}
    for header, dependencies in graph.items():
        owner = component_by_header[header]
        for dependency in dependencies:
            dependency_owner = component_by_header[dependency]
            if owner != dependency_owner:
                component_graph[owner].add(dependency_owner)

    ranks = {}

    def rank(component):
        if component not in ranks:
            dependencies = component_graph[component]
            ranks[component] = (
                0
                if not dependencies
                else 1 + max(rank(dependency) for dependency in dependencies)
            )
        return ranks[component]

    for component in component_graph:
        rank(component)

    public_headers = {
        header for header, header_consumers in consumers.items() if header_consumers
    }
    private_headers = set(graph) - public_headers

    raw_groups = defaultdict(list)
    for header in public_headers:
        header_rank = ranks[component_by_header[header]]
        # Rank-zero headers are independent primitives.  Keeping them in their
        # own semantic categories avoids making every consumer of ob_errno.h,
        # an atomic, or a logging constant inherit all other primitives.  The
        # remaining ranks use five-level bands to keep the target count
        # practical without introducing one-header forwarding targets.
        band_start = (
            0
            if header_rank == 0
            else 1 + ((header_rank - 1) // 5) * 5
        )
        raw_groups[(_header_category(header), band_start)].append(header)

    raw_owner = {
        header: group for group, members in raw_groups.items() for header in members
    }
    raw_graph = {group: set() for group in raw_groups}
    for header in public_headers:
        for dependency in graph[header]:
            if dependency in public_headers:
                source_group = raw_owner[header]
                dependency_group = raw_owner[dependency]
                if source_group != dependency_group:
                    raw_graph[source_group].add(dependency_group)

    merged_groups = _tarjan(raw_graph)
    owner_group_by_raw_group = {
        raw_group: merged_group
        for merged_group in merged_groups
        for raw_group in merged_group
    }

    owner_name_by_group = {}
    used_names = set()
    for merged_group in sorted(
        merged_groups,
        key=lambda group: (
            min(item[1] for item in group),
            tuple(sorted(item[0] for item in group)),
        ),
    ):
        band_start = min(item[1] for item in merged_group)
        categories = sorted({item[0] for item in merged_group})
        if len(categories) > 1 and set(categories).issubset(
            {"collections", "concurrency", "core", "diagnostics", "memory"}
        ):
            category_name = "foundation"
        elif set(categories) == {"data_formats", "db_values"}:
            category_name = "database_model"
        else:
            category_name = "_".join(categories)
        tier = TIER_NAMES[band_start]
        base_name = "oblib_%s_%s" % (category_name, tier)
        preferred_names = {
            ("compression", "model"): "oblib_compression",
            ("core", "primitives"): "oblib_foundation",
            ("db_values", "domain"): "oblib_common",
            ("restore", "integration"): "oblib_restore",
            ("rpc_transport", "runtime"): "oblib_rpc",
            ("sql_mode", "primitives"): "common_sql_mode",
            ("vector", "utilities"): "oblib_vector",
        }
        base_name = preferred_names.get((category_name, tier), base_name)
        name = base_name
        suffix = 2
        while name in used_names:
            name = "%s_%d" % (base_name, suffix)
            suffix += 1
        used_names.add(name)
        owner_name_by_group[merged_group] = name

    owner_by_header = {}
    owner_specs = {}
    for merged_group, name in owner_name_by_group.items():
        headers = sorted(
            header
            for raw_group in merged_group
            for header in raw_groups[raw_group]
        )
        owner_specs[name] = {
            "hdrs": headers,
            "deps": set(),
            "third_party_deps": set(),
            "needs_easy": False,
        }
        for header in headers:
            owner_by_header[header] = name
            owner_specs[name]["third_party_deps"].update(
                third_party_deps[header]
            )
            owner_specs[name]["needs_easy"] = (
                owner_specs[name]["needs_easy"] or easy_deps[header]
            )

    for header in public_headers:
        owner = owner_by_header[header]
        for dependency in graph[header]:
            if dependency in public_headers:
                dependency_owner = owner_by_header[dependency]
                if owner != dependency_owner:
                    owner_specs[owner]["deps"].add(":" + dependency_owner)

    for spec in owner_specs.values():
        spec["deps"].update(spec["third_party_deps"])
        if spec["needs_easy"]:
            spec["deps"].add("//src/oblib/easy:easy")

    return owner_specs, owner_by_header, sorted(private_headers)


def _render(owner_specs, owner_by_header, private_headers):
    lines = [
        '"""Generated explicit OBLib header ownership; do not edit by hand."""',
        "",
        "OBLIB_HEADER_TARGETS = {",
    ]
    for name in sorted(owner_specs):
        spec = owner_specs[name]
        lines.extend(
            [
                '    "%s": {' % name,
                '        "hdrs": %s,' % _quote_list(spec["hdrs"]),
                '        "deps": %s,' % _quote_list(sorted(spec["deps"])),
                "    },",
            ]
        )
    lines.extend(["}", "", "OBLIB_HEADER_TARGET_FOR_HEADER = {"])
    for header in sorted(owner_by_header):
        lines.append(
            '    "%s": ":%s",' % (header, owner_by_header[header])
        )
    lines.extend([
        "}",
        "",
        "OBLIB_PRIVATE_HEADERS = %s" % _quote_list(private_headers, "    "),
        "",
        "OBLIB_ALL_HEADER_TARGETS = [",
    ])
    for name in sorted(owner_specs):
        lines.append('    ":%s",' % name)
    lines.extend(["]", "", "OBLIB_ALL_THIRD_PARTY_HEADER_DEPS = ["])
    for label in ALL_THIRD_PARTY_DEPS:
        lines.append('    "%s",' % label)
    lines.extend(["]", ""])
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail if the checked-in inventory differs from generated output",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="write to this path instead of stdout",
    )
    arguments = parser.parse_args()

    repo = Path(__file__).resolve().parents[2]
    default_output = repo / "src" / "oblib" / "oblib_header_inventory.bzl"
    output = arguments.output or default_output
    graph, consumers, easy_deps, third_party_deps = _collect_graph(repo)
    rendered = _render(
        *_build_inventory(graph, consumers, easy_deps, third_party_deps)
    )
    if arguments.check:
        if not output.is_file() or _read(output) != rendered:
            print("OBLib header inventory is stale: %s" % output, file=sys.stderr)
            return 1
        return 0
    if arguments.output:
        output.write_text(rendered, encoding="utf-8")
    else:
        sys.stdout.write(rendered)
    return 0


if __name__ == "__main__":
    sys.exit(main())
