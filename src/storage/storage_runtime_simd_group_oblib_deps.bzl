"""Generated exact OBLib dependencies for semantic Unity groups."""

_STORAGE_RUNTIME_SIMD_GROUP_OBLIB_DEPS = {
    "ob_storage_simd_common_0": [
        "//src/oblib:common_sql_mode",
        "//src/oblib:oblib_collections_advanced",
        "//src/oblib:oblib_collections_model",
        "//src/oblib:oblib_collections_primitives",
        "//src/oblib:oblib_collections_runtime",
        "//src/oblib:oblib_collections_utilities",
        "//src/oblib:oblib_common",
        "//src/oblib:oblib_compression",
        "//src/oblib:oblib_compression_support",
        "//src/oblib:oblib_concurrency_advanced",
        "//src/oblib:oblib_concurrency_primitives",
        "//src/oblib:oblib_concurrency_runtime_base",
        "//src/oblib:oblib_concurrency_support",
        "//src/oblib:oblib_core_advanced",
        "//src/oblib:oblib_core_utilities",
        "//src/oblib:oblib_data_formats_domain",
        "//src/oblib:oblib_data_formats_model",
        "//src/oblib:oblib_db_log_advanced",
        "//src/oblib:oblib_db_log_primitives",
        "//src/oblib:oblib_db_meta_domain",
        "//src/oblib:oblib_db_meta_runtime",
        "//src/oblib:oblib_db_values_advanced",
        "//src/oblib:oblib_db_values_integration",
        "//src/oblib:oblib_db_values_model",
        "//src/oblib:oblib_db_values_primitives",
        "//src/oblib:oblib_db_values_runtime",
        "//src/oblib:oblib_db_values_services",
        "//src/oblib:oblib_diagnostics_runtime",
        "//src/oblib:oblib_encoding_runtime",
        "//src/oblib:oblib_foundation",
        "//src/oblib:oblib_foundation_base",
        "//src/oblib:oblib_foundation_integration",
        "//src/oblib:oblib_foundation_runtime",
        "//src/oblib:oblib_foundation_services",
        "//src/oblib:oblib_foundation_support",
        "//src/oblib:oblib_io_runtime",
        "//src/oblib:oblib_memory_advanced",
        "//src/oblib:oblib_memory_base",
        "//src/oblib:oblib_memory_model",
        "//src/oblib:oblib_memory_runtime_base",
        "//src/oblib:oblib_memory_utilities",
        "//src/oblib:oblib_mysql_client_domain",
        "//src/oblib:oblib_mysql_client_runtime",
        "//src/oblib:oblib_restore_advanced",
    ],
}

def storage_runtime_simd_group_oblib_deps(groups, base = {}):
    actual = {group.name: True for group in groups}
    unknown = sorted([
        name
        for name in _STORAGE_RUNTIME_SIMD_GROUP_OBLIB_DEPS
        if name not in actual
    ])
    missing = sorted([
        name
        for name in actual
        if name not in _STORAGE_RUNTIME_SIMD_GROUP_OBLIB_DEPS
    ])
    if unknown or missing:
        fail("OBLib Unity dependency map differs from inventory: unknown=%s missing=%s" % (unknown, missing))
    return {
        name: base.get(name, []) + _STORAGE_RUNTIME_SIMD_GROUP_OBLIB_DEPS[name]
        for name in actual
    }
