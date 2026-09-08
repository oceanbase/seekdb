# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
if(NOT EXISTS "${SEEKDB_WASM_DEPS}/lib/libxml2.a")
  message(FATAL_ERROR "Build libxml2 with tools/wasm/build-libxml2.sh")
endif()
add_library(seekdb_wasm_xml STATIC IMPORTED GLOBAL)
set_target_properties(seekdb_wasm_xml PROPERTIES
  IMPORTED_LOCATION "${SEEKDB_WASM_DEPS}/lib/libxml2.a"
  INTERFACE_INCLUDE_DIRECTORIES "${SEEKDB_WASM_DEPS}/include/libxml2")
target_link_libraries(seekdb_wasm_engine_link_probe PRIVATE seekdb_wasm_xml)
add_executable(test_wasm_xml "${SEEKDB_ROOT}/unittest/wasm/test_wasm_xml.cpp")
target_link_libraries(test_wasm_xml PRIVATE seekdb_wasm_engine_options
  seekdb_wasm_oblib_common seekdb_wasm_oblib_lib seekdb_wasm_xml
  seekdb_wasm_charset seekdb_wasm_memory seekdb_wasm_runtime_support)
target_compile_options(test_wasm_xml PRIVATE -UNDEBUG)
target_link_options(test_wasm_xml PRIVATE -pthread -sUSE_ZLIB=1
  -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=1048576 -sDEFAULT_PTHREAD_STACK_SIZE=1048576
  -sPTHREAD_POOL_SIZE=3 -sPTHREAD_POOL_SIZE_STRICT=2 -sASSERTIONS=2
  -sEXIT_RUNTIME=1 -sABORTING_MALLOC=0 -Wl,--error-limit=0)
add_test(NAME wasm_xml COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  "$<TARGET_FILE:test_wasm_xml>")
set_tests_properties(wasm_xml PROPERTIES TIMEOUT 30)
