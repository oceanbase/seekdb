"""OBLib-owned build topology exposed to this package's BUILD file."""

load(
    ":oblib_module_sources.bzl",
    _oblib_exclude_unity_sources = "oblib_exclude_unity_sources",
    _oblib_select_unity_groups = "oblib_select_unity_groups",
    _oblib_standalone_sources = "oblib_standalone_sources",
)
load(
    ":oblib_source_inventory.bzl",
    _OBLIB_STANDALONE_SOURCES = "OBLIB_STANDALONE_SOURCES",
    _OBLIB_UNITY_GROUPS = "OBLIB_UNITY_GROUPS",
)

OBLIB_STANDALONE_SOURCES = _OBLIB_STANDALONE_SOURCES
OBLIB_UNITY_GROUPS = _OBLIB_UNITY_GROUPS
oblib_exclude_unity_sources = _oblib_exclude_unity_sources
oblib_select_unity_groups = _oblib_select_unity_groups
oblib_standalone_sources = _oblib_standalone_sources
