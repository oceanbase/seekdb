seekdb_add_plugin(seekdb_gis_plugin
  SOURCES seekdb_gis_plugin.c geometry_engine.cpp
  OUTPUT_NAME seekdb_gis
  MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/plugin.toml)

# libstdc++ template instantiations used by the private algorithm engine must
# not become part of the plugin ABI.  Export exactly the C entry point.
if (UNIX AND NOT APPLE)
  target_link_options(seekdb_gis_plugin PRIVATE
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map"
    -static-libstdc++ -static-libgcc)
endif()
