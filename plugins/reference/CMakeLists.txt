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

seekdb_add_plugin(seekdb_reference_stop_blocked_plugin
  SOURCES seekdb_reference_blocked_plugin.c
  OUTPUT_NAME seekdb_reference_stop_blocked
  MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/stop_blocked_plugin.toml
  NO_INSTALL)
target_compile_definitions(seekdb_reference_stop_blocked_plugin PRIVATE
  SEEKDB_BLOCKED_PLUGIN_ID="org.seekdb.reference.stop-blocked"
  SEEKDB_BLOCKED_PLUGIN_BUILD_ID="reference-stop-blocked-abi-v1"
  SEEKDB_BLOCKED_PLUGIN_FAIL_START=0)
