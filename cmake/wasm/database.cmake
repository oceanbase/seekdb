# Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
# Browser/Node module with the real database lifecycle and memory protocol ABI.
add_executable(seekdb_wasm_database EXCLUDE_FROM_ALL "${SEEKDB_ROOT}/src/wasm/database_runtime.cpp")
get_target_property(seekdb_wasm_database_libraries seekdb_wasm_engine_link_probe LINK_LIBRARIES)
target_link_libraries(seekdb_wasm_database PRIVATE ${seekdb_wasm_database_libraries})
set_target_properties(seekdb_wasm_database PROPERTIES SUFFIX ".mjs")
# Keep the deployable JS facade and Worker next to the generated module/Wasm.
# configure_file also makes source updates trigger CMake regeneration.
foreach(module database database-worker worker-server runtime-host mysql-client mysql-transport mysql-wire mysql-auth)
  configure_file("${SEEKDB_ROOT}/src/wasm/${module}.mjs" "${CMAKE_CURRENT_BINARY_DIR}/${module}.mjs" COPYONLY)
endforeach()
configure_file("${SEEKDB_ROOT}/unittest/wasm/database-browser.html"
  "${CMAKE_CURRENT_BINARY_DIR}/database-browser.html" COPYONLY)
configure_file("${SEEKDB_ROOT}/src/wasm/database-console.html"
  "${CMAKE_CURRENT_BINARY_DIR}/database-console.html" COPYONLY)
configure_file("${SEEKDB_ROOT}/unittest/wasm/database-browser-cases.mjs"
  "${CMAKE_CURRENT_BINARY_DIR}/database-browser-cases.mjs" COPYONLY)
target_link_options(seekdb_wasm_database PRIVATE
  -Oz --emit-symbol-map -pthread -msimd128 -sUSE_ZLIB=1 -sPROXY_TO_PTHREAD=1
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,worker,node
  -sPTHREAD_POOL_SIZE=64 -sPTHREAD_POOL_SIZE_STRICT=2 -sDEFAULT_PTHREAD_STACK_SIZE=1048576
  -sINITIAL_MEMORY=536870912 -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=2147483648
  -sSTACK_SIZE=2097152 -sASSERTIONS=2 -sEXIT_RUNTIME=1 -Wl,--error-limit=0
  "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_nio_memory_read','_nio_memory_write','_nio_memory_close']"
  "-sEXPORTED_RUNTIME_METHODS=['HEAPU8','getValue']")

add_executable(test_wasm_log_ring EXCLUDE_FROM_ALL "${SEEKDB_ROOT}/unittest/wasm/test_log_ring.cpp")
target_link_libraries(test_wasm_log_ring PRIVATE ${seekdb_wasm_database_libraries})
target_compile_options(test_wasm_log_ring PRIVATE -UNDEBUG)
target_link_options(test_wasm_log_ring PRIVATE -Oz -pthread -msimd128 -sUSE_ZLIB=1
  -sPROXY_TO_PTHREAD=1 -sPTHREAD_POOL_SIZE=5 -sPTHREAD_POOL_SIZE_STRICT=2
  -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=1048576 -sASSERTIONS=2 -sEXIT_RUNTIME=1)
