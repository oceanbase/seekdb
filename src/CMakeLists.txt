add_subdirectory(oblib)

add_library(ob_base_without_pass INTERFACE)
add_library(ob_base INTERFACE)
# src gets -Isrc from its own base target(no longer relies on oblib_base interface leakage),
# so oblib_base_without_pass can safely remove ${CMAKE_SOURCE_DIR}/src without affecting src targets。
# attached to ob_base_without_pass(ob_base inherits it through linking), covers targets that directly link ob_base_without_pass(e.g. ob_share_extra)。
target_include_directories(
  ob_base_without_pass INTERFACE
  ${CMAKE_SOURCE_DIR}
  # Generated inner-table headers must shadow the checked-in snapshots.  The
  # schema definition is authoritative and may intentionally remove entries.
  ${CMAKE_BINARY_DIR}/generated
  # Historical code uses share/inner_table/..., inner_table/..., and basename
  # spellings for generated schema headers. Preserve all three without
  # modifying the source tree.
  ${CMAKE_BINARY_DIR}/generated/share
  ${CMAKE_BINARY_DIR}/generated/share/inner_table
  ${CMAKE_CURRENT_SOURCE_DIR}
  # Query and Data Plane keep their public include spelling stable while the
  # physical headers live below module-local api roots.
  ${CMAKE_CURRENT_SOURCE_DIR}/query/api
  ${CMAKE_CURRENT_SOURCE_DIR}/data_plane/api
  ${CMAKE_CURRENT_SOURCE_DIR}/objit/include)

target_compile_features(ob_base_without_pass INTERFACE cxx_std_11)
target_link_libraries(ob_base_without_pass INTERFACE oblib_base_without_pass
  ${OB_RELRO_FLAG}
  $<$<PLATFORM_ID:Linux>:-Wl,-T,${CMAKE_SOURCE_DIR}/rpm/ld.lds>)
target_compile_definitions(ob_base_without_pass INTERFACE
  PACKAGE_NAME="${PROJECT_NAME}"
  PACKAGE_VERSION="${PROJECT_VERSION}"
  PACKAGE_STRING="${PROJECT_NAME} ${PROJECT_VERSION}"
  # change to real release ID.
  RELEASEID="${BUILD_NUMBER}"
  )
target_compile_options(ob_base_without_pass INTERFACE
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-invalid-offsetof>)

target_link_libraries(ob_base INTERFACE ob_base_without_pass oblib_pass)
function(ob_server_add_target target)
  message(STATUS "observer_add_target ${target}")
  ob_add_object_target(${target})
  target_link_libraries(${target} PUBLIC ob_base)
  list(APPEND ob_objects ${target})
  set(ob_objects "${ob_objects}" CACHE INTERNAL "observer object library list")
endfunction()

add_subdirectory(share)
add_subdirectory(data_plane)
add_subdirectory(query)
add_subdirectory(sql)
add_subdirectory(pl)
add_subdirectory(rootserver)
add_subdirectory(logservice)
add_subdirectory(storage)
add_subdirectory(observer)
