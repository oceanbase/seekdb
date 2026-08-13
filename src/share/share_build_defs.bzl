"""Share-owned build topology exposed to its BUILD file."""

load(
    ":inner_table_schema.bzl",
    _inner_table_schema_cpp_outputs = "inner_table_schema_cpp_outputs",
    _inner_table_schema_source_replacements = "inner_table_schema_source_replacements",
)
load(
    ":share_header_inventory.bzl",
    _SHARE_IMPLEMENTATION_HEADERS = "SHARE_IMPLEMENTATION_HEADERS",
    _SHARE_INTERFACE_CLOSURE_HEADERS = "SHARE_INTERFACE_CLOSURE_HEADERS",
    _SHARE_PRIVATE_HEADERS = "SHARE_PRIVATE_HEADERS",
    _SHARE_PUBLIC_HEADER_ROOTS = "SHARE_PUBLIC_HEADER_ROOTS",
)
load(
    ":share_module_sources.bzl",
    _share_remaining_unity_groups = "share_remaining_unity_groups",
    _share_select_unity_groups = "share_select_unity_groups",
    _share_select_unity_sources = "share_select_unity_sources",
    _share_singleton_unity_groups = "share_singleton_unity_groups",
    _share_unity_group_sources = "share_unity_group_sources",
    _share_validate_source_inventory = "share_validate_source_inventory",
)
load(
    ":share_source_inventory.bzl",
    _SHARE_DATUM_STANDALONE_SOURCES = "SHARE_DATUM_STANDALONE_SOURCES",
    _SHARE_STANDALONE_SOURCES = "SHARE_STANDALONE_SOURCES",
    _SHARE_UNITY_GROUPS = "SHARE_UNITY_GROUPS",
)

SHARE_DATUM_STANDALONE_SOURCES = _SHARE_DATUM_STANDALONE_SOURCES
SHARE_IMPLEMENTATION_HEADERS = _SHARE_IMPLEMENTATION_HEADERS
SHARE_INTERFACE_CLOSURE_HEADERS = _SHARE_INTERFACE_CLOSURE_HEADERS
SHARE_PRIVATE_HEADERS = _SHARE_PRIVATE_HEADERS
SHARE_PUBLIC_HEADER_ROOTS = _SHARE_PUBLIC_HEADER_ROOTS
SHARE_STANDALONE_SOURCES = _SHARE_STANDALONE_SOURCES
SHARE_UNITY_GROUPS = _SHARE_UNITY_GROUPS
inner_table_schema_cpp_outputs = _inner_table_schema_cpp_outputs
inner_table_schema_source_replacements = _inner_table_schema_source_replacements
share_remaining_unity_groups = _share_remaining_unity_groups
share_select_unity_groups = _share_select_unity_groups
share_select_unity_sources = _share_select_unity_sources
share_singleton_unity_groups = _share_singleton_unity_groups
share_unity_group_sources = _share_unity_group_sources
share_validate_source_inventory = _share_validate_source_inventory
