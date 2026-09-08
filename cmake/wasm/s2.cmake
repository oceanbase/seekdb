# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
if(NOT EXISTS "${SEEKDB_WASM_DEPS}/lib/libs2.a")
  message(FATAL_ERROR "Build S2 and Abseil with tools/wasm/build-s2.sh")
endif()
find_package(absl CONFIG REQUIRED PATHS "${SEEKDB_WASM_DEPS}/lib/cmake/absl"
  NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
add_library(seekdb_wasm_s2 STATIC IMPORTED GLOBAL)
set_target_properties(seekdb_wasm_s2 PROPERTIES
  IMPORTED_LOCATION "${SEEKDB_WASM_DEPS}/lib/libs2.a"
  INTERFACE_INCLUDE_DIRECTORIES "${SEEKDB_WASM_DEPS}/include")
# Match the pinned upstream s2 target's Abseil dependency graph.
target_link_libraries(seekdb_wasm_s2 INTERFACE seekdb_wasm_crypto
  absl::base absl::btree absl::config absl::core_headers absl::dynamic_annotations
  absl::endian absl::fixed_array absl::flat_hash_map absl::flat_hash_set absl::hash
  absl::inlined_vector absl::int128 absl::log_severity absl::memory absl::span
  absl::str_format absl::strings absl::type_traits absl::utility)
target_link_libraries(seekdb_wasm_engine_link_probe PRIVATE seekdb_wasm_s2)
add_executable(test_wasm_geometry "${SEEKDB_ROOT}/unittest/wasm/test_wasm_geometry.cpp"
  "${SEEKDB_ROOT}/src/share/geo/ob_s2adapter.cpp"
  "${SEEKDB_ROOT}/src/share/geo/ob_geo_tree.cpp"
  "${SEEKDB_ROOT}/src/share/geo/ob_geo_bin.cpp"
  "${SEEKDB_ROOT}/src/share/geo/ob_geo_common.cpp")
target_link_libraries(test_wasm_geometry PRIVATE seekdb_wasm_engine_options
  seekdb_wasm_s2 seekdb_wasm_oblib_common seekdb_wasm_oblib_lib
  seekdb_wasm_charset seekdb_wasm_memory seekdb_wasm_runtime_support)
target_compile_options(test_wasm_geometry PRIVATE -UNDEBUG)
target_link_options(test_wasm_geometry PRIVATE -pthread -sUSE_ZLIB=1
  -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=1048576 -sDEFAULT_PTHREAD_STACK_SIZE=1048576
  -sPTHREAD_POOL_SIZE=3 -sPTHREAD_POOL_SIZE_STRICT=2 -sASSERTIONS=2
  -sEXIT_RUNTIME=1 -sABORTING_MALLOC=0 -Wl,--error-limit=0)
add_test(NAME wasm_geometry COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  "$<TARGET_FILE:test_wasm_geometry>")
set_tests_properties(wasm_geometry PROPERTIES TIMEOUT 30)
