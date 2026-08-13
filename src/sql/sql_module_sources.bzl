"""Helpers for declaring SQL's native semantic source graph."""

_SQL_SOURCE_PREFIX = "src/sql/"

SQL_OPTIMIZER_GROUP_NAMES = [
    "ob_sql_optimizer_0",
    "ob_sql_optimizer_1",
    "ob_sql_optimizer_2",
    "ob_sql_optimizer_stat_0",
    "ob_sql_optimizer_stat_1",
]

SQL_PREPARE_SOURCES = [
    "src/sql/engine/prepare/ob_deallocate_executor.cpp",
    "src/sql/engine/prepare/ob_execute_executor.cpp",
    "src/sql/engine/prepare/ob_prepare_executor.cpp",
    "src/sql/resolver/prepare/ob_deallocate_resolver.cpp",
    "src/sql/resolver/prepare/ob_deallocate_stmt.cpp",
    "src/sql/resolver/prepare/ob_execute_resolver.cpp",
    "src/sql/resolver/prepare/ob_execute_stmt.cpp",
    "src/sql/resolver/prepare/ob_prepare_resolver.cpp",
    "src/sql/resolver/prepare/ob_prepare_stmt.cpp",
]

# These generated-template families make the custom LLVM Sanity pass more than
# 3.8x slower in a representative local action. Keep them as standalone
# translation units with ENABLE_SANITY, but make the compile-cost exception
# explicit by not loading the pass.
SQL_SANITY_WITHOUT_PASS_STANDALONE_PATHS = []

def _unity_group_key(group):
    return group.language + ":" + group.name

def sql_headers_outside(headers, prefixes):
    """Returns root-package headers after semantic subpackage ownership."""

    return [
        header
        for header in headers
        if not any([header.startswith(prefix) for prefix in prefixes])
    ]

def sql_paths_under(paths, prefix):
    """Returns package-relative paths owned below one prefix."""

    return [
        path[len(prefix):]
        for path in paths
        if path.startswith(prefix)
    ]

def sql_headers_under(headers, prefix):
    """Returns package-relative headers owned below one semantic prefix."""

    return sql_paths_under(headers, prefix)

def sql_groups_named(groups, names):
    """Returns the named groups while rejecting a stale semantic selection."""

    wanted = {name: False for name in names}
    selected = []
    for group in groups:
        if group.name in wanted:
            wanted[group.name] = True
            selected.append(group)
    missing = sorted([name for name, found in wanted.items() if not found])
    if missing:
        fail("unknown SQL Unity groups: %s" % missing)
    return selected

def sql_groups_without(groups, excluded_group_names, excluded_sources):
    """Removes separately-owned semantic groups and sources from root SQL."""

    excluded_groups = {name: False for name in excluded_group_names}
    excluded_paths = {path: False for path in excluded_sources}
    result = []
    for group in groups:
        if group.name in excluded_groups:
            excluded_groups[group.name] = True
            continue
        srcs = []
        for path in group.srcs:
            if path in excluded_paths:
                excluded_paths[path] = True
            else:
                srcs.append(path)
        if srcs:
            result.append(struct(
                name = group.name,
                language = group.language,
                srcs = srcs,
                generated_srcs = group.generated_srcs,
                external_srcs = group.external_srcs,
            ))

    missing_groups = sorted([
        name
        for name, found in excluded_groups.items()
        if not found
    ])
    missing_paths = sorted([
        path
        for path, found in excluded_paths.items()
        if not found
    ])
    if missing_groups or missing_paths:
        fail(
            "stale SQL semantic exclusions: groups=%s sources=%s" %
            (missing_groups, missing_paths),
        )
    return result

def sql_partition_sources(sources, selected_paths):
    """Partitions source structs while rejecting stale selected paths."""

    selected_state = {path: False for path in selected_paths}
    selected = []
    remaining = []
    for source in sources:
        if source.path in selected_state:
            selected_state[source.path] = True
            selected.append(source)
        else:
            remaining.append(source)

    missing = sorted([
        path
        for path, found in selected_state.items()
        if not found
    ])
    if missing:
        fail("unknown SQL source selection: %s" % missing)
    return struct(
        selected = selected,
        remaining = remaining,
    )

def sql_singleton_unity_groups(sources, name_prefix, language = "c++"):
    """Preserves standalone compile-action granularity in one SQL module."""

    groups = []
    for index, source in enumerate(sources):
        if source.kind != "source":
            fail("non-source SQL input: %s (%s)" % (source.path, source.kind))
        if source.language != language:
            fail("%s expects %s, got %s" % (source.path, language, source.language))
        if not source.path.startswith(_SQL_SOURCE_PREFIX):
            fail("source outside src/sql: %s" % source.path)
        groups.append(struct(
            name = "%s_%d" % (name_prefix, index),
            language = language,
            srcs = [source.path],
            generated_srcs = [],
            external_srcs = [],
        ))
    return groups

def sql_validate_source_inventory(
        unity_groups,
        simd_unity_groups,
        standalone_sources,
        extra_sources,
        parser_sources,
        separately_owned_sources):
    """Freezes SQL's checked-in source and release Unity baselines."""

    group_keys = {}
    unity_paths = {}
    for group in unity_groups + simd_unity_groups:
        key = _unity_group_key(group)
        if key in group_keys:
            fail("duplicate SQL Unity group: %s" % key)
        group_keys[key] = True
        if group.generated_srcs:
            fail("SQL Unity group %s still has generated_srcs" % key)
        if group.external_srcs:
            fail("SQL Unity group %s still has external_srcs" % key)
        for path in group.srcs:
            if not path.startswith(_SQL_SOURCE_PREFIX):
                fail("SQL Unity source is outside src/sql: %s" % path)
            if path in unity_paths:
                fail("duplicate SQL Unity source: %s" % path)
            unity_paths[path] = True

    standalone_paths = {}
    for source in standalone_sources + extra_sources:
        if source.kind != "source":
            fail("non-source SQL standalone input: %s" % source.path)
        if not source.path.startswith(_SQL_SOURCE_PREFIX):
            fail("SQL standalone source is outside src/sql: %s" % source.path)
        if source.path in standalone_paths or source.path in unity_paths:
            fail("duplicate SQL standalone source: %s" % source.path)
        standalone_paths[source.path] = True

    parser_paths = {}
    for path in parser_sources:
        if not path.startswith("src/sql/parser/"):
            fail("parser source is outside src/sql/parser: %s" % path)
        if path in parser_paths or path in unity_paths or path in standalone_paths:
            fail("duplicate SQL parser source: %s" % path)
        parser_paths[path] = True

    separate_paths = {}
    for path in separately_owned_sources:
        if not path.startswith("src/sql/"):
            fail("separately owned source is outside src/sql: %s" % path)
        if path in separate_paths or path in unity_paths or path in standalone_paths or path in parser_paths:
            fail("duplicate separately owned SQL source: %s" % path)
        separate_paths[path] = True

    if len(unity_groups) != 67:
        fail("SQL inventory must contain 67 regular Unity groups, got %s" % len(unity_groups))
    if len(simd_unity_groups) != 1:
        fail("SQL inventory must contain 1 SIMD Unity group, got %s" % len(simd_unity_groups))
    if len(unity_paths) != 1110:
        fail("SQL inventory must contain 1110 Unity sources, got %s" % len(unity_paths))
    if len(standalone_paths) != 26:
        fail("SQL inventory must contain 26 standalone sources, got %s" % len(standalone_paths))
    if len(parser_paths) != 15:
        fail("SQL inventory must contain 15 checked-in parser sources, got %s" % len(parser_paths))
    if len(separate_paths) != 7:
        fail("SQL inventory must contain 7 separately owned sources, got %s" % len(separate_paths))
    if len(unity_paths) + len(standalone_paths) + len(parser_paths) + len(separate_paths) != 1158:
        fail("SQL checked-in source ownership must cover exactly 1158 files")

    sql_groups_named(unity_groups, SQL_OPTIMIZER_GROUP_NAMES)
    sql_groups_without(unity_groups, SQL_OPTIMIZER_GROUP_NAMES, SQL_PREPARE_SOURCES)
