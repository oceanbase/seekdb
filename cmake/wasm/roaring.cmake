# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
if(NOT EXISTS "${SEEKDB_WASM_DEPS}/lib/libroaring.a")
  message(FATAL_ERROR "Build CRoaring with tools/wasm/build-roaring.sh")
endif()
add_library(seekdb_wasm_roaring STATIC IMPORTED GLOBAL)
set_target_properties(seekdb_wasm_roaring PROPERTIES
  IMPORTED_LOCATION "${SEEKDB_WASM_DEPS}/lib/libroaring.a"
  INTERFACE_INCLUDE_DIRECTORIES "${SEEKDB_WASM_DEPS}/include")
target_link_libraries(seekdb_wasm_engine_link_probe PRIVATE seekdb_wasm_roaring)
add_executable(test_wasm_roaring "${SEEKDB_ROOT}/unittest/wasm/test_wasm_roaring.cpp")
target_link_libraries(test_wasm_roaring PRIVATE seekdb_wasm_roaring)
target_compile_options(test_wasm_roaring PRIVATE -pthread -Oz -UNDEBUG)
target_link_options(test_wasm_roaring PRIVATE -pthread -Oz -sASSERTIONS=2 -sEXIT_RUNTIME=1)
add_test(NAME wasm_roaring COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  "$<TARGET_FILE:test_wasm_roaring>")
set_tests_properties(wasm_roaring PROPERTIES TIMEOUT 30)
