# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
set(engine_inventory "${CMAKE_CURRENT_BINARY_DIR}/wasm-engine-sources.cmake")
file(GLOB engine_inventory_inputs CONFIGURE_DEPENDS
  "${SEEKDB_ROOT}/src/*/*source_inventory.bzl"
  "${SEEKDB_ROOT}/src/*/*build_defs.bzl")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${engine_inventory_inputs}
  "${SEEKDB_ROOT}/tools/wasm/emit-engine-sources.py"
  "${SEEKDB_ROOT}/tools/cmake/emit_bazel_source_inventory.py")
execute_process(COMMAND "${Python3_EXECUTABLE}"
  "${SEEKDB_ROOT}/tools/wasm/emit-engine-sources.py"
  --repo "${SEEKDB_ROOT}" --output "${engine_inventory}"
  COMMAND_ERROR_IS_FATAL ANY)
include("${engine_inventory}")

add_library(seekdb_wasm_engine_options INTERFACE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SEEKDB_ROOT}/CMakeLists.txt")
file(STRINGS "${SEEKDB_ROOT}/CMakeLists.txt" engine_version_line REGEX "^  VERSION [0-9]+")
string(REGEX MATCH "[0-9]+(\\.[0-9]+)+" engine_version "${engine_version_line}")
if(NOT engine_version)
  message(FATAL_ERROR "Cannot read engine version from the production CMake project")
endif()
target_compile_definitions(seekdb_wasm_engine_options INTERFACE
  PACKAGE_NAME="seekdb" PACKAGE_VERSION="${engine_version}"
  PACKAGE_STRING="seekdb ${engine_version} WebAssembly")
target_compile_features(seekdb_wasm_engine_options INTERFACE cxx_std_20)
target_compile_options(seekdb_wasm_engine_options INTERFACE
  -pthread -Oz -Wno-register -sUSE_ZLIB=1)
target_include_directories(seekdb_wasm_engine_options INTERFACE
  "${SEEKDB_ROOT}" "${SEEKDB_ROOT}/src" "${SEEKDB_ROOT}/include"
  "${SEEKDB_ROOT}/src/oblib" "${SEEKDB_ROOT}/src/oblib/easy"
  "${SEEKDB_ROOT}/src/oblib/common" "${SEEKDB_ROOT}/src/objit/include"
  "${SEEKDB_ROOT}/rust/sql-nio/include"
  "${SEEKDB_ROOT}/src/oblib/easy/include" "${SEEKDB_ROOT}/src/query/api"
  "${SEEKDB_ROOT}/src/data_plane/api" "${SEEKDB_ROOT}/src/sql/parser"
  "${CMAKE_CURRENT_BINARY_DIR}/generated" "${parser_generated_dir}"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/share"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/share/inner_table"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/observer/virtual_table"
  "${CMAKE_CURRENT_BINARY_DIR}/third_party/include")
target_link_libraries(seekdb_wasm_engine_options INTERFACE seekdb_wasm_crypto)

# Keep the complete native module membership visible while individual platform
# dependencies are being ported. These compile targets are not a runnable engine.
function(seekdb_wasm_engine_module name)
  set(sources)
  foreach(prefix IN LISTS ARGN)
    if(DEFINED ${prefix}_GROUPS)
      foreach(group IN LISTS ${prefix}_GROUPS)
        list(APPEND sources ${${prefix}_GROUP_${group}})
        set_source_files_properties(${${prefix}_GROUP_${group}} PROPERTIES
          UNITY_GROUP "${name}_${group}")
      endforeach()
    else()
      list(APPEND sources ${${prefix}})
      set_source_files_properties(${${prefix}} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    endif()
  endforeach()
  list(REMOVE_DUPLICATES sources)
  add_library(seekdb_wasm_${name} STATIC EXCLUDE_FROM_ALL ${sources})
  set_target_properties(seekdb_wasm_${name} PROPERTIES UNITY_BUILD ON UNITY_BUILD_MODE GROUP)
  target_link_libraries(seekdb_wasm_${name} PRIVATE seekdb_wasm_engine_options)
endfunction()

seekdb_wasm_engine_module(sql SEEKDB_SQL_UNITY SEEKDB_SQL_SIMD_UNITY
  SEEKDB_SQL_STANDALONE SEEKDB_SQL_EXTRA)
seekdb_wasm_engine_module(storage SEEKDB_STORAGE_UNITY SEEKDB_STORAGE_SIMD_UNITY
  SEEKDB_STORAGE_STANDALONE SEEKDB_STORAGE_EXTRA SEEKDB_STORAGE_TABLET_AUTOINCREMENT_STATE)
seekdb_wasm_engine_module(share SEEKDB_SHARE_UNITY SEEKDB_SHARE_STANDALONE
  SEEKDB_SHARE_DATUM_STANDALONE)
seekdb_wasm_engine_module(observer SEEKDB_OBSERVER_UNITY SEEKDB_OBSERVER_STANDALONE
  SEEKDB_OBSERVER_RETRIEVAL_COMPOSITION)
seekdb_wasm_engine_module(logservice SEEKDB_LOGSERVICE_UNITY)
seekdb_wasm_engine_module(rootserver SEEKDB_ROOTSERVER_UNITY SEEKDB_ROOTSERVER_STANDALONE)
seekdb_wasm_engine_module(pl SEEKDB_PL_UNITY SEEKDB_PL_STANDALONE)
# These extracted modules use one runtime Unity group in the native CMake
# build (three and two sources, below its batch limit).
set(SEEKDB_WASM_QUERY_GROUPS runtime)
set(SEEKDB_WASM_QUERY_GROUP_runtime ${SEEKDB_QUERY_MYSQL_PROTOCOL}
  ${SEEKDB_QUERY_SCHEDULER} ${SEEKDB_QUERY_VECTOR_EMBEDDING})
set(SEEKDB_WASM_DATA_PLANE_GROUPS runtime)
set(SEEKDB_WASM_DATA_PLANE_GROUP_runtime ${SEEKDB_DATA_PLANE_PARALLEL_RANGE}
  ${SEEKDB_DATA_PLANE_TABLET_SCAN})
seekdb_wasm_engine_module(query SEEKDB_WASM_QUERY)
seekdb_wasm_engine_module(data_plane SEEKDB_WASM_DATA_PLANE)
seekdb_wasm_engine_module(oblib_common SEEKDB_OBLIB_COMMON_UNITY SEEKDB_OBLIB_COMMON_STANDALONE)
seekdb_wasm_engine_module(oblib_lib SEEKDB_OBLIB_LIB_UNITY SEEKDB_OBLIB_BITMAP_UNITY
  SEEKDB_OBLIB_SIMD_UNITY)
# The codec implementation maps its SSE4.1 intrinsics through the pinned SDK.
target_compile_options(seekdb_wasm_oblib_lib PRIVATE -msimd128 -msse4.1)
# Preserve the native non-Unity action for the log compressor.
set_source_files_properties("${SEEKDB_ROOT}/src/oblib/lib/oblog/ob_log_compressor.cpp"
  PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
seekdb_wasm_engine_module(compression SEEKDB_OBLIB_COMPRESS_STANDALONE SEEKDB_OBLIB_ZSTD_STANDALONE)
seekdb_wasm_engine_module(restore SEEKDB_OBLIB_RESTORE_STANDALONE)
seekdb_wasm_engine_module(rpc SEEKDB_OBLIB_RPC_UNITY)
add_custom_target(seekdb_wasm_engine_objects DEPENDS
  seekdb_wasm_sql seekdb_wasm_storage seekdb_wasm_share seekdb_wasm_observer
  seekdb_wasm_logservice seekdb_wasm_rootserver seekdb_wasm_pl
  seekdb_wasm_query seekdb_wasm_data_plane seekdb_wasm_oblib_common
  seekdb_wasm_oblib_lib seekdb_wasm_compression seekdb_wasm_restore seekdb_wasm_rpc)
