# Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
add_executable(test_wasm_ptr_spin_lock "${SEEKDB_ROOT}/unittest/wasm/test_wasm_ptr_spin_lock.cpp")
target_compile_features(test_wasm_ptr_spin_lock PRIVATE cxx_std_20)
target_include_directories(test_wasm_ptr_spin_lock PRIVATE
  "${SEEKDB_ROOT}/src/oblib" "${SEEKDB_ROOT}/src/oblib/easy"
  "${SEEKDB_ROOT}/src/oblib/easy/include" "${SEEKDB_ROOT}/src" "${SEEKDB_ROOT}/include")
target_compile_options(test_wasm_ptr_spin_lock PRIVATE -Oz -UNDEBUG -pthread)
target_link_options(test_wasm_ptr_spin_lock PRIVATE -Oz -pthread -sASSERTIONS=2
  -sPROXY_TO_PTHREAD=1 -sPTHREAD_POOL_SIZE=3 -sPTHREAD_POOL_SIZE_STRICT=2 -sEXIT_RUNTIME=1)
add_test(NAME wasm_ptr_spin_lock COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  "$<TARGET_FILE:test_wasm_ptr_spin_lock>")
set_tests_properties(wasm_ptr_spin_lock PROPERTIES TIMEOUT 30)
