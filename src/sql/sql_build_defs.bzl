"""SQL-owned build topology shared by SQL BUILD packages."""

load(
    ":sql_header_inventory.bzl",
    _SQL_COMPOSITION_HEADERS = "SQL_COMPOSITION_HEADERS",
    _SQL_INTERFACE_CLOSURE_HEADERS = "SQL_INTERFACE_CLOSURE_HEADERS",
    _SQL_PRIVATE_HEADERS = "SQL_PRIVATE_HEADERS",
    _SQL_PUBLIC_HEADER_ROOTS = "SQL_PUBLIC_HEADER_ROOTS",
)
load(
    ":sql_module_sources.bzl",
    _SQL_OPTIMIZER_GROUP_NAMES = "SQL_OPTIMIZER_GROUP_NAMES",
    _SQL_PREPARE_SOURCES = "SQL_PREPARE_SOURCES",
    _SQL_SANITY_WITHOUT_PASS_STANDALONE_PATHS = "SQL_SANITY_WITHOUT_PASS_STANDALONE_PATHS",
    _sql_groups_named = "sql_groups_named",
    _sql_groups_without = "sql_groups_without",
    _sql_headers_outside = "sql_headers_outside",
    _sql_headers_under = "sql_headers_under",
    _sql_partition_sources = "sql_partition_sources",
    _sql_paths_under = "sql_paths_under",
    _sql_singleton_unity_groups = "sql_singleton_unity_groups",
    _sql_validate_source_inventory = "sql_validate_source_inventory",
)
load(
    ":sql_runtime_deps.bzl",
    _SQL_RUNTIME_NATIVE_DEPS = "SQL_RUNTIME_NATIVE_DEPS",
)
load(
    ":sql_source_inventory.bzl",
    _SQL_EXTRA_SOURCES = "SQL_EXTRA_SOURCES",
    _SQL_PARSER_SOURCES = "SQL_PARSER_SOURCES",
    _SQL_SIMD_UNITY_GROUPS = "SQL_SIMD_UNITY_GROUPS",
    _SQL_STANDALONE_SOURCES = "SQL_STANDALONE_SOURCES",
    _SQL_UNITY_GROUPS = "SQL_UNITY_GROUPS",
)

SQL_COMPOSITION_HEADERS = _SQL_COMPOSITION_HEADERS
SQL_EXTRA_SOURCES = _SQL_EXTRA_SOURCES
SQL_INTERFACE_CLOSURE_HEADERS = _SQL_INTERFACE_CLOSURE_HEADERS
SQL_OPTIMIZER_GROUP_NAMES = _SQL_OPTIMIZER_GROUP_NAMES
SQL_PARSER_SOURCES = _SQL_PARSER_SOURCES
SQL_PREPARE_SOURCES = _SQL_PREPARE_SOURCES
SQL_SANITY_WITHOUT_PASS_STANDALONE_PATHS = _SQL_SANITY_WITHOUT_PASS_STANDALONE_PATHS
SQL_PRIVATE_HEADERS = _SQL_PRIVATE_HEADERS
SQL_PUBLIC_HEADER_ROOTS = _SQL_PUBLIC_HEADER_ROOTS
SQL_RUNTIME_NATIVE_DEPS = _SQL_RUNTIME_NATIVE_DEPS
SQL_SIMD_UNITY_GROUPS = _SQL_SIMD_UNITY_GROUPS
SQL_STANDALONE_SOURCES = _SQL_STANDALONE_SOURCES
SQL_UNITY_GROUPS = _SQL_UNITY_GROUPS
sql_groups_named = _sql_groups_named
sql_groups_without = _sql_groups_without
sql_headers_outside = _sql_headers_outside
sql_headers_under = _sql_headers_under
sql_partition_sources = _sql_partition_sources
sql_paths_under = _sql_paths_under
sql_singleton_unity_groups = _sql_singleton_unity_groups
sql_validate_source_inventory = _sql_validate_source_inventory
