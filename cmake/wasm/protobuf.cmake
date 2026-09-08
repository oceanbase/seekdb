# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
if(NOT EXISTS "${SEEKDB_WASM_DEPS}/lib/libprotobuf-c.a")
  message(FATAL_ERROR "Build protobuf-c with tools/wasm/build-protobuf-c.sh")
endif()
add_library(seekdb_wasm_protobuf STATIC IMPORTED GLOBAL)
set_target_properties(seekdb_wasm_protobuf PROPERTIES
  IMPORTED_LOCATION "${SEEKDB_WASM_DEPS}/lib/libprotobuf-c.a"
  INTERFACE_INCLUDE_DIRECTORIES "${SEEKDB_WASM_DEPS}/include")
target_link_libraries(seekdb_wasm_engine_link_probe PRIVATE seekdb_wasm_protobuf)
add_executable(test_wasm_vector_tile "${SEEKDB_ROOT}/unittest/wasm/test_wasm_vector_tile.cpp"
  "${SEEKDB_ROOT}/src/share/geo/ob_vector_tile.pb-c.c")
target_include_directories(test_wasm_vector_tile PRIVATE "${SEEKDB_ROOT}/src")
target_link_libraries(test_wasm_vector_tile PRIVATE seekdb_wasm_protobuf)
target_compile_features(test_wasm_vector_tile PRIVATE cxx_std_20)
target_compile_options(test_wasm_vector_tile PRIVATE -pthread -Oz -UNDEBUG)
target_link_options(test_wasm_vector_tile PRIVATE -pthread -Oz -sASSERTIONS=2 -sEXIT_RUNTIME=1)
add_test(NAME wasm_vector_tile COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  "$<TARGET_FILE:test_wasm_vector_tile>")
set_tests_properties(wasm_vector_tile PROPERTIES TIMEOUT 30)
