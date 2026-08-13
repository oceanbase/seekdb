"""Rootserver-owned build topology exposed to its BUILD file."""

load(
    ":rootserver_header_inventory.bzl",
    _ROOTSERVER_PRIVATE_HEADERS = "ROOTSERVER_PRIVATE_HEADERS",
    _ROOTSERVER_PUBLIC_HEADERS = "ROOTSERVER_PUBLIC_HEADERS",
    _rootserver_validate_header_inventory = "rootserver_validate_header_inventory",
)
load(
    ":rootserver_module_sources.bzl",
    _rootserver_singleton_unity_groups = "rootserver_singleton_unity_groups",
    _rootserver_validate_source_inventory = "rootserver_validate_source_inventory",
)
load(
    ":rootserver_source_inventory.bzl",
    _ROOTSERVER_IGNORED_SOURCES = "ROOTSERVER_IGNORED_SOURCES",
    _ROOTSERVER_STANDALONE_SOURCES = "ROOTSERVER_STANDALONE_SOURCES",
    _ROOTSERVER_UNITY_GROUPS = "ROOTSERVER_UNITY_GROUPS",
)

ROOTSERVER_IGNORED_SOURCES = _ROOTSERVER_IGNORED_SOURCES
ROOTSERVER_PRIVATE_HEADERS = _ROOTSERVER_PRIVATE_HEADERS
ROOTSERVER_PUBLIC_HEADERS = _ROOTSERVER_PUBLIC_HEADERS
ROOTSERVER_STANDALONE_SOURCES = _ROOTSERVER_STANDALONE_SOURCES
ROOTSERVER_UNITY_GROUPS = _ROOTSERVER_UNITY_GROUPS
rootserver_singleton_unity_groups = _rootserver_singleton_unity_groups
rootserver_validate_header_inventory = _rootserver_validate_header_inventory
rootserver_validate_source_inventory = _rootserver_validate_source_inventory
