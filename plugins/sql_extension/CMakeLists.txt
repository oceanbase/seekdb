seekdb_add_plugin(seekdb_sql_extension_plugin
  SOURCES seekdb_sql_extension_plugin.c
  OUTPUT_NAME seekdb_sql_extension
  MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/plugin.toml
  NO_INSTALL)
