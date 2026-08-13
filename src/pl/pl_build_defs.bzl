"""PL-owned build topology exposed to its BUILD file."""

load(
    ":pl_header_inventory.bzl",
    _PL_IGNORED_GENERATED_HEADERS = "PL_IGNORED_GENERATED_HEADERS",
    _PL_PRIVATE_HEADERS = "PL_PRIVATE_HEADERS",
    _PL_PUBLIC_HEADER_ROOTS = "PL_PUBLIC_HEADER_ROOTS",
    _pl_validate_header_inventory = "pl_validate_header_inventory",
)
load(
    ":pl_module_sources.bzl",
    _pl_singleton_unity_groups = "pl_singleton_unity_groups",
    _pl_validate_source_inventory = "pl_validate_source_inventory",
)
load(
    ":pl_source_inventory.bzl",
    _PL_IGNORED_GENERATED_SOURCES = "PL_IGNORED_GENERATED_SOURCES",
    _PL_STANDALONE_SOURCES = "PL_STANDALONE_SOURCES",
    _PL_UNITY_GROUPS = "PL_UNITY_GROUPS",
)

PL_IGNORED_GENERATED_HEADERS = _PL_IGNORED_GENERATED_HEADERS
PL_IGNORED_GENERATED_SOURCES = _PL_IGNORED_GENERATED_SOURCES
PL_PRIVATE_HEADERS = _PL_PRIVATE_HEADERS
PL_PUBLIC_HEADER_ROOTS = _PL_PUBLIC_HEADER_ROOTS
PL_STANDALONE_SOURCES = _PL_STANDALONE_SOURCES
PL_UNITY_GROUPS = _PL_UNITY_GROUPS
pl_singleton_unity_groups = _pl_singleton_unity_groups
pl_validate_header_inventory = _pl_validate_header_inventory
pl_validate_source_inventory = _pl_validate_source_inventory
