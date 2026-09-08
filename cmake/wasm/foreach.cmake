# Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
add_executable(test_wasm_foreach "${SEEKDB_ROOT}/unittest/wasm/test_wasm_foreach.cpp")
target_compile_features(test_wasm_foreach PRIVATE cxx_std_20)
target_include_directories(test_wasm_foreach PRIVATE "${SEEKDB_ROOT}/src/oblib")
target_compile_options(test_wasm_foreach PRIVATE -Oz -UNDEBUG -fsanitize=address)
target_link_options(test_wasm_foreach PRIVATE -Oz -fsanitize=address)
add_test(NAME wasm_foreach COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  "$<TARGET_FILE:test_wasm_foreach>")
set_tests_properties(wasm_foreach PROPERTIES TIMEOUT 30)
