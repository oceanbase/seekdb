# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
add_executable(test_wasm_rpc_memory
  "${SEEKDB_ROOT}/unittest/wasm/test_wasm_rpc_memory.cpp"
  "${SEEKDB_ROOT}/src/oblib/rpc/ob_sql_mem_pool.cpp")
target_link_libraries(test_wasm_rpc_memory PRIVATE seekdb_wasm_engine_options
  seekdb_wasm_memory seekdb_wasm_runtime_support)
target_compile_options(test_wasm_rpc_memory PRIVATE -UNDEBUG)
target_link_options(test_wasm_rpc_memory PRIVATE -pthread -sUSE_ZLIB=1
  -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=1048576 -sASSERTIONS=2
  -sEXIT_RUNTIME=1 -sABORTING_MALLOC=0)
add_test(NAME wasm_rpc_memory COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  "$<TARGET_FILE:test_wasm_rpc_memory>")
set_tests_properties(wasm_rpc_memory PROPERTIES TIMEOUT 30)
