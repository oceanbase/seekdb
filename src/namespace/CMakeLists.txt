# Namespace identity and catalog metadata: STL-only, with no deps on other src modules.
set(ob_namespace_cache_objects_ catalog.cpp namespace.cpp)
set_source_files_properties(catalog.cpp namespace.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
ob_server_add_target(ob_namespace)
