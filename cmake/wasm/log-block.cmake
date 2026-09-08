# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
add_executable(test_wasm_log_block "${SEEKDB_ROOT}/unittest/wasm/test_wasm_log_block.cpp"
  "${SEEKDB_ROOT}/src/logservice/ob_server_log_block_mgr.cpp"
  "${SEEKDB_ROOT}/src/logservice/palf/log_io_utils.cpp"
  "${SEEKDB_ROOT}/src/logservice/palf/log_block_pool_interface.cpp"
  "${SEEKDB_ROOT}/src/share/log/palf/log_define.cpp"
  "${SEEKDB_ROOT}/src/oblib/lib/file/file_directory_utils.cpp")
target_link_libraries(test_wasm_log_block PRIVATE seekdb_wasm_engine_options
  seekdb_wasm_memory seekdb_wasm_runtime_support)
target_compile_options(test_wasm_log_block PRIVATE -UNDEBUG)
target_link_options(test_wasm_log_block PRIVATE -Oz -pthread -sUSE_ZLIB=1
  -sINITIAL_MEMORY=268435456 -sSTACK_SIZE=1048576 -sASSERTIONS=2
  -sPROXY_TO_PTHREAD=1 -sPTHREAD_POOL_SIZE=2 -sPTHREAD_POOL_SIZE_STRICT=2
  -sEXIT_RUNTIME=1 -sABORTING_MALLOC=0 -Wl,--wrap=pwrite -Wl,--error-limit=0)
add_test(NAME wasm_log_block COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  "$<TARGET_FILE:test_wasm_log_block>")
set_tests_properties(wasm_log_block PROPERTIES TIMEOUT 30)
