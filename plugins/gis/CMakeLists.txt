include("${CMAKE_CURRENT_LIST_DIR}/BoostGeometry.cmake")
seekdb_mark_plugin_private_library(gis_boost_geometry)
include("${CMAKE_CURRENT_LIST_DIR}/S2.cmake")

seekdb_add_plugin(seekdb_gis_plugin
  SOURCES seekdb_gis_plugin.c geometry_engine.cpp number_format.cpp projection.cpp spatial_index.cpp srs_parser.cpp srs_metadata.cpp srs_service.cpp
  PRIVATE_LIBRARIES gis_boost_geometry gis_s2
  OUTPUT_NAME seekdb_gis
  MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/plugin.toml)
target_compile_features(seekdb_gis_plugin PRIVATE cxx_std_17)

# libstdc++ template instantiations used by the private algorithm engine must
# not become part of the plugin ABI.  Export exactly the C entry point.
if (UNIX AND NOT APPLE)
  target_link_options(seekdb_gis_plugin PRIVATE
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map"
    -static-libstdc++ -static-libgcc)
endif()
