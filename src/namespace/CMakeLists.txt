# Namespace identity layer: STL-only, with no deps on other src modules.
set(ob_namespace_cache_objects_ namespace.cpp)
set_source_files_properties(namespace.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
ob_server_add_target(ob_namespace)
