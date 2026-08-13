"""Observer-owned build topology exposed to its BUILD file."""

load(
    ":observer_header_inventory.bzl",
    _OBSERVER_PRIVATE_HEADERS = "OBSERVER_PRIVATE_HEADERS",
)
load(
    ":observer_module_sources.bzl",
    _observer_singleton_unity_groups = "observer_singleton_unity_groups",
    _observer_validate_header_inventory = "observer_validate_header_inventory",
    _observer_validate_source_inventory = "observer_validate_source_inventory",
)
load(
    ":observer_source_inventory.bzl",
    _OBSERVER_IGNORED_SOURCES = "OBSERVER_IGNORED_SOURCES",
    _OBSERVER_MAIN_SOURCES = "OBSERVER_MAIN_SOURCES",
    _OBSERVER_RETRIEVAL_COMPOSITION_SOURCES = "OBSERVER_RETRIEVAL_COMPOSITION_SOURCES",
    _OBSERVER_STANDALONE_SOURCES = "OBSERVER_STANDALONE_SOURCES",
    _OBSERVER_UNITY_GROUPS = "OBSERVER_UNITY_GROUPS",
)

OBSERVER_IGNORED_SOURCES = _OBSERVER_IGNORED_SOURCES
OBSERVER_MAIN_SOURCES = _OBSERVER_MAIN_SOURCES
OBSERVER_PRIVATE_HEADERS = _OBSERVER_PRIVATE_HEADERS
OBSERVER_RETRIEVAL_COMPOSITION_SOURCES = _OBSERVER_RETRIEVAL_COMPOSITION_SOURCES
OBSERVER_STANDALONE_SOURCES = _OBSERVER_STANDALONE_SOURCES
OBSERVER_UNITY_GROUPS = _OBSERVER_UNITY_GROUPS
observer_singleton_unity_groups = _observer_singleton_unity_groups
observer_validate_header_inventory = _observer_validate_header_inventory
observer_validate_source_inventory = _observer_validate_source_inventory
