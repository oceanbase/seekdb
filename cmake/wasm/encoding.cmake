# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
add_executable(test_wasm_order_encoding
  "${SEEKDB_ROOT}/unittest/wasm/test_wasm_order_encoding.cpp"
  "${SEEKDB_ROOT}/src/storage/ob_order_perserving_encoder.cpp")
target_link_libraries(test_wasm_order_encoding PRIVATE seekdb_wasm_engine_options
  seekdb_wasm_memory seekdb_wasm_runtime_support)
target_compile_options(test_wasm_order_encoding PRIVATE -UNDEBUG)
target_link_options(test_wasm_order_encoding PRIVATE -pthread -sUSE_ZLIB=1
  -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=1048576 -sASSERTIONS=2
  -sEXIT_RUNTIME=1 -sABORTING_MALLOC=0)
add_test(NAME wasm_order_encoding COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  "$<TARGET_FILE:test_wasm_order_encoding>")
set_tests_properties(wasm_order_encoding PROPERTIES TIMEOUT 30)

add_executable(test_wasm_integer_codec
  "${SEEKDB_ROOT}/unittest/wasm/test_wasm_integer_codec.cpp"
  "${SEEKDB_ROOT}/src/oblib/lib/checksum/ob_crc64.cpp"
  "${SEEKDB_ROOT}/src/oblib/lib/codec/ob_fast_delta.cpp"
  "${SEEKDB_ROOT}/src/oblib/lib/codec/ob_generated_unalign_simd_bp_func.cpp")
target_link_libraries(test_wasm_integer_codec PRIVATE seekdb_wasm_engine_options
  seekdb_wasm_memory seekdb_wasm_runtime_support)
target_compile_options(test_wasm_integer_codec PRIVATE -UNDEBUG -msimd128 -msse4.1)
target_link_options(test_wasm_integer_codec PRIVATE -pthread -msimd128 -msse4.1
  -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=1048576 -sASSERTIONS=2
  -sEXIT_RUNTIME=1 -sABORTING_MALLOC=0)
add_test(NAME wasm_integer_codec COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  "$<TARGET_FILE:test_wasm_integer_codec>")
set_tests_properties(wasm_integer_codec PROPERTIES TIMEOUT 30)
