add_subdirectory(reference EXCLUDE_FROM_ALL)
if (SEEKDB_ENABLE_EXPERIMENTAL_PLUGINS)
  add_subdirectory(gis)
endif()
