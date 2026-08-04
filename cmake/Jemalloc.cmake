if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID AND NOT OB_USE_ASAN)
  set(JEMALLOC_STATIC_LIBRARY "${DEP_DIR}/lib/libjemalloc_pic.a")
  if(NOT EXISTS "${JEMALLOC_STATIC_LIBRARY}")
    message(FATAL_ERROR
      "Packaged jemalloc not found: ${JEMALLOC_STATIC_LIBRARY}")
  endif()

  add_library(bundled_jemalloc STATIC IMPORTED GLOBAL)
  set_target_properties(bundled_jemalloc PROPERTIES
    IMPORTED_LOCATION "${JEMALLOC_STATIC_LIBRARY}")
  set(OB_HAVE_BUNDLED_JEMALLOC TRUE)
  message(STATUS "Using packaged jemalloc: ${JEMALLOC_STATIC_LIBRARY}")
elseif(APPLE AND NOT OB_USE_ASAN)
  set(JEMALLOC_STATIC_LIBRARY "${DEP_DIR}/lib/libjemalloc_pic.a")
  if(EXISTS "${JEMALLOC_STATIC_LIBRARY}")
    add_library(bundled_jemalloc STATIC IMPORTED GLOBAL)
    set_target_properties(bundled_jemalloc PROPERTIES
      IMPORTED_LOCATION "${JEMALLOC_STATIC_LIBRARY}")
    set(OB_HAVE_BUNDLED_JEMALLOC TRUE)
    message(STATUS "Using packaged jemalloc: ${JEMALLOC_STATIC_LIBRARY}")
  else()
    message(STATUS "Packaged jemalloc not found; using obmalloc on macOS")
  endif()
endif()
