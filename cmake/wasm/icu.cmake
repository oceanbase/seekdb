# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
foreach(component common i18n data)
  if(NOT EXISTS "${SEEKDB_WASM_DEPS}/lib/libicu_${component}.a")
    message(FATAL_ERROR "Build ICU 69.1 with tools/wasm/build-icu.sh")
  endif()
  add_library(seekdb_wasm_icu_${component} STATIC IMPORTED GLOBAL)
  set_target_properties(seekdb_wasm_icu_${component} PROPERTIES
    IMPORTED_LOCATION "${SEEKDB_WASM_DEPS}/lib/libicu_${component}.a"
    INTERFACE_INCLUDE_DIRECTORIES "${SEEKDB_WASM_DEPS}/include")
endforeach()
target_link_libraries(seekdb_wasm_icu_i18n INTERFACE seekdb_wasm_icu_common)
target_link_libraries(seekdb_wasm_icu_common INTERFACE seekdb_wasm_icu_data)
target_link_libraries(seekdb_wasm_engine_link_probe PRIVATE seekdb_wasm_icu_i18n)
add_executable(test_wasm_regex "${SEEKDB_ROOT}/unittest/wasm/test_wasm_regex.cpp"
  "${SEEKDB_ROOT}/src/sql/engine/expr/ob_expr_regexp_context.cpp"
  "${SEEKDB_ROOT}/src/sql/engine/expr/ob_expr_operator.cpp")
target_link_libraries(test_wasm_regex PRIVATE seekdb_wasm_engine_options
  seekdb_wasm_icu_i18n seekdb_wasm_oblib_common seekdb_wasm_oblib_lib
  seekdb_wasm_charset seekdb_wasm_memory seekdb_wasm_runtime_support)
target_compile_options(test_wasm_regex PRIVATE -UNDEBUG)
target_link_options(test_wasm_regex PRIVATE -pthread -sUSE_ZLIB=1
  -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=1048576 -sDEFAULT_PTHREAD_STACK_SIZE=1048576
  -sPTHREAD_POOL_SIZE=3 -sPTHREAD_POOL_SIZE_STRICT=2 -sASSERTIONS=2
  -sEXIT_RUNTIME=1 -sABORTING_MALLOC=0 -Wl,--error-limit=0)
add_test(NAME wasm_regex COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  "$<TARGET_FILE:test_wasm_regex>")
set_tests_properties(wasm_regex PROPERTIES TIMEOUT 30)
