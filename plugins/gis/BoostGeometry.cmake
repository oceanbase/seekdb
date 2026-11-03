# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0.

# Use precisely the dependency tree used by the core, never FindBoost's
# accidental /usr/include fallback. This interface target adds headers only.
if(NOT TARGET gis_boost_geometry)
  if(NOT SEEKDB_SOURCE_DIR)
    set(_gis_source_root "${PROJECT_SOURCE_DIR}")
  else()
    set(_gis_source_root "${SEEKDB_SOURCE_DIR}")
  endif()
  if(DEP_DIR)
    set(_gis_boost_include "${DEP_DIR}/include")
  else()
    set(_gis_boost_include "${_gis_source_root}/deps/3rd/usr/local/oceanbase/deps/devel/include")
  endif()
  if(NOT EXISTS "${_gis_boost_include}/boost/geometry.hpp")
    message(FATAL_ERROR "GIS requires SeekDB's prepared Boost headers: ${_gis_boost_include}")
  endif()
  add_library(gis_boost_geometry INTERFACE IMPORTED GLOBAL)
  set_target_properties(gis_boost_geometry PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_gis_boost_include}")
endif()
