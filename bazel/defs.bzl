"""Stable public entry point for seekdb's first-party Bazel rules.

BUILD files load project rules from here. The implementation files remain free
to evolve without spreading their layout or standard policy across packages.
"""

load("@rules_cc//cc:cc_import.bzl", _cc_import = "cc_import")
load(
    "//bazel:seekdb_build_config.bzl",
    _SEEKDB_C_MIGRATION_COPTS = "SEEKDB_C_MIGRATION_COPTS",
    _SEEKDB_MIGRATION_COPTS = "SEEKDB_MIGRATION_COPTS",
    _SEEKDB_OBLIB_COPTS = "SEEKDB_OBLIB_COPTS",
    _SEEKDB_OBLIB_C_COPTS = "SEEKDB_OBLIB_C_COPTS",
    _SEEKDB_OBLIB_LOCAL_DEFINES = "SEEKDB_OBLIB_LOCAL_DEFINES",
    _SEEKDB_SEMANTIC_MODULE_COPTS = "SEEKDB_SEMANTIC_MODULE_COPTS",
    _SEEKDB_X86_SIMD_COPTS = "SEEKDB_X86_SIMD_COPTS",
    _seekdb_openmp_copts = "seekdb_openmp_copts",
)
load(
    "//bazel:seekdb_cc_library.bzl",
    _seekdb_cc_library = "seekdb_cc_library",
    _seekdb_generated_unity_cc_library = "seekdb_generated_unity_cc_library",
    _seekdb_generated_unity_source = "seekdb_generated_unity_source",
    _seekdb_multi_unity_cc_library = "seekdb_multi_unity_cc_library",
    _seekdb_semantic_unity_cc_library = "seekdb_semantic_unity_cc_library",
    _seekdb_source_ownership_check = "seekdb_source_ownership_check",
    _seekdb_unity_cc_library = "seekdb_unity_cc_library",
)
load("//bazel:seekdb_cc_test.bzl", _seekdb_cc_test = "seekdb_cc_test")
load("//bazel:seekdb_module_cc_test.bzl", _seekdb_module_cc_test = "seekdb_module_cc_test")
load("//bazel:seekdb_final_link.bzl", _seekdb_final_link = "seekdb_final_link")
load("//bazel:seekdb_partial_link.bzl", _seekdb_localized_partial_link = "seekdb_localized_partial_link")
load("//bazel:seekdb_test_interface.bzl", _seekdb_test_interface = "seekdb_test_interface")
load("//bazel:seekdb_version.bzl", _seekdb_version_source = "seekdb_version_source")

SEEKDB_C_MIGRATION_COPTS = _SEEKDB_C_MIGRATION_COPTS
SEEKDB_MIGRATION_COPTS = _SEEKDB_MIGRATION_COPTS
SEEKDB_OBLIB_COPTS = _SEEKDB_OBLIB_COPTS
SEEKDB_OBLIB_C_COPTS = _SEEKDB_OBLIB_C_COPTS
SEEKDB_OBLIB_LOCAL_DEFINES = _SEEKDB_OBLIB_LOCAL_DEFINES
SEEKDB_SEMANTIC_MODULE_COPTS = _SEEKDB_SEMANTIC_MODULE_COPTS
SEEKDB_X86_SIMD_COPTS = _SEEKDB_X86_SIMD_COPTS
seekdb_openmp_copts = _seekdb_openmp_copts

cc_import = _cc_import
seekdb_cc_library = _seekdb_cc_library
seekdb_cc_test = _seekdb_cc_test
seekdb_module_cc_test = _seekdb_module_cc_test
seekdb_final_link = _seekdb_final_link
seekdb_generated_unity_cc_library = _seekdb_generated_unity_cc_library
seekdb_generated_unity_source = _seekdb_generated_unity_source
seekdb_localized_partial_link = _seekdb_localized_partial_link
seekdb_multi_unity_cc_library = _seekdb_multi_unity_cc_library
seekdb_semantic_unity_cc_library = _seekdb_semantic_unity_cc_library
seekdb_source_ownership_check = _seekdb_source_ownership_check
seekdb_test_interface = _seekdb_test_interface
seekdb_unity_cc_library = _seekdb_unity_cc_library
seekdb_version_source = _seekdb_version_source
