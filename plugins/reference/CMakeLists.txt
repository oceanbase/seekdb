seekdb_add_plugin(seekdb_reference_plugin
  SOURCES seekdb_reference_plugin.c
  OUTPUT_NAME seekdb_reference
  MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/plugin.toml
  NO_INSTALL)

seekdb_add_plugin(seekdb_reference_blocked_plugin
  SOURCES seekdb_reference_blocked_plugin.c
  OUTPUT_NAME seekdb_reference_blocked
  MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/blocked_plugin.toml
  NO_INSTALL)

seekdb_add_plugin(seekdb_reference_invalid_extensions_plugin
  SOURCES seekdb_reference_invalid_extensions_plugin.c
  OUTPUT_NAME seekdb_reference_invalid_extensions
  MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/invalid_extensions_plugin.toml
  NO_INSTALL)

seekdb_add_plugin(seekdb_reference_invalid_manifest_plugin
  SOURCES seekdb_reference_invalid_extensions_plugin.c
  OUTPUT_NAME seekdb_reference_invalid_manifest
  MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/invalid_manifest_plugin.toml
  NO_INSTALL)
target_compile_definitions(seekdb_reference_invalid_manifest_plugin PRIVATE
  SEEKDB_INVALID_EXTENSIONS_PLUGIN_ID="org.seekdb.reference.invalid-manifest"
  SEEKDB_INVALID_EXTENSIONS_PLUGIN_BUILD_ID="reference-invalid-manifest-abi-v1"
  SEEKDB_INVALID_EXTENSIONS_MANIFEST_CAPABILITIES=SEEKDB_PLUGIN_CAPABILITY_EXTENSION_CATALOG)

seekdb_add_plugin(seekdb_reference_stop_blocked_plugin
  SOURCES seekdb_reference_blocked_plugin.c
  OUTPUT_NAME seekdb_reference_stop_blocked
  MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/stop_blocked_plugin.toml
  NO_INSTALL)
target_compile_definitions(seekdb_reference_stop_blocked_plugin PRIVATE
  SEEKDB_BLOCKED_PLUGIN_ID="org.seekdb.reference.stop-blocked"
  SEEKDB_BLOCKED_PLUGIN_BUILD_ID="reference-stop-blocked-abi-v1"
  SEEKDB_BLOCKED_PLUGIN_FAIL_START=0)

seekdb_add_plugin(seekdb_reference_registration_conflict_plugin
  SOURCES seekdb_reference_registration_conflict_plugin.c
  OUTPUT_NAME seekdb_reference_registration_conflict
  MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/registration_conflict_plugin.toml
  NO_INSTALL)
