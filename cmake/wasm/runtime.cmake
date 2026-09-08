# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(SEEKDB_WASM_INVENTORY "${CMAKE_CURRENT_BINARY_DIR}/wasm-runtime-sources.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${SEEKDB_ROOT}/src/oblib/oblib_source_inventory.bzl"
  "${SEEKDB_ROOT}/src/sql/sql_source_inventory.bzl"
  "${SEEKDB_ROOT}/tools/wasm/emit-runtime-sources.py"
  "${SEEKDB_ROOT}/tools/wasm/runtime-components.json"
  "${SEEKDB_ROOT}/tools/cmake/emit_bazel_source_inventory.py")
execute_process(COMMAND "${Python3_EXECUTABLE}"
  "${SEEKDB_ROOT}/tools/wasm/emit-runtime-sources.py"
  --repo "${SEEKDB_ROOT}" --output "${SEEKDB_WASM_INVENTORY}"
  COMMAND_ERROR_IS_FATAL ANY)
include("${SEEKDB_WASM_INVENTORY}")
add_library(seekdb_wasm_memory_objects OBJECT EXCLUDE_FROM_ALL ${SEEKDB_WASM_ALLOCATOR_SOURCES})
target_compile_features(seekdb_wasm_memory_objects PRIVATE cxx_std_20)
target_compile_options(seekdb_wasm_memory_objects PRIVATE -pthread)
target_include_directories(seekdb_wasm_memory_objects PRIVATE
  "${SEEKDB_ROOT}/src/oblib" "${SEEKDB_ROOT}/src/oblib/easy"
  "${SEEKDB_ROOT}/src/oblib/easy/include" "${SEEKDB_ROOT}/src" "${SEEKDB_ROOT}/include")
add_library(seekdb_wasm_memory STATIC EXCLUDE_FROM_ALL $<TARGET_OBJECTS:seekdb_wasm_memory_objects>)
add_library(seekdb_wasm_runtime_support STATIC EXCLUDE_FROM_ALL ${SEEKDB_WASM_RUNTIME_SUPPORT_SOURCES})
target_compile_features(seekdb_wasm_runtime_support PRIVATE cxx_std_20)
target_compile_options(seekdb_wasm_runtime_support PRIVATE -pthread -sUSE_ZLIB=1)
target_include_directories(seekdb_wasm_runtime_support PRIVATE
  "${SEEKDB_ROOT}/src/oblib" "${SEEKDB_ROOT}/src/oblib/easy"
  "${SEEKDB_ROOT}/src/oblib/easy/include" "${SEEKDB_ROOT}/src" "${SEEKDB_ROOT}/include")
