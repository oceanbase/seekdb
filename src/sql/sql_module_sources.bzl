"""Helpers for declaring SQL's native semantic source graph."""

OPTIMIZER_GROUP_NAMES = [
    "ob_sql_optimizer_0",
    "ob_sql_optimizer_join_order",
    "ob_sql_optimizer_operators",
    "ob_sql_optimizer_log_plan",
    "ob_sql_optimizer_1",
    "ob_sql_optimizer_cost",
    "ob_sql_optimizer_utils",
    "ob_sql_optimizer_2",
    "ob_sql_optimizer_stat_0",
    "ob_sql_optimizer_stat_1",
]

PREPARE_SOURCES = [
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

def headers_outside(headers, prefixes):
    """Returns root-package headers after semantic subpackage ownership."""

    return [
        header
        for header in headers
        if not any([header.startswith(prefix) for prefix in prefixes])
    ]

def paths_under(paths, prefix):
    """Returns package-relative paths owned below one prefix."""

    return [
        path[len(prefix):]
        for path in paths
        if path.startswith(prefix)
    ]


def groups_without(groups, excluded_group_names, excluded_sources):
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

def validate_plugin_sources(groups, standalone, adapters, runtime, replaced):
    """Keep host adapters uniquely owned and reject stale GIS replacements."""
    baseline = [path for group in groups for path in group.srcs] + standalone
    seen = {}
    for path in baseline + adapters + runtime:
        if not path.startswith("src/sql/") or ".." in path.split("/"):
            fail("SQL source outside src/sql: " + path)
        if path in seen:
            fail("duplicate SQL source: " + path)
        seen[path] = True
    if len(adapters) != 2 or len(runtime) != 4:
        fail("SQL plugin inventory requires 2 GIS adapters and 4 runtime sources")
    if len(replaced) != 1 or replaced[0] not in baseline:
        fail("GIS replacement must reference a distinct baseline owner")
