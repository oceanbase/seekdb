cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(@@NAME@@ LANGUAGES C)
  set(SEEKDB_SOURCE_DIR @@ROOT_CMAKE@@ CACHE PATH "Matching seekdb source checkout")
  include("${SEEKDB_SOURCE_DIR}/cmake/RustPlugin.cmake")
  seekdb_add_rust_plugin(seekdb_@@NAME@@_plugin STANDALONE
    LIBRARY_NAME seekdb_@@NAME@@ MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/plugin.toml")
else()
  # When explicitly added under seekdb/plugins/, use the normal in-tree gate.
  seekdb_add_rust_plugin(seekdb_@@NAME@@_plugin
    LIBRARY_NAME seekdb_@@NAME@@ MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/plugin.toml")
endif()
