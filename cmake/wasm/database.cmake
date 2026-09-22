# Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
# Browser/Node module with the real database lifecycle and memory protocol ABI.
# WasmFS provides both storage modes: its memory backend by default, and the
# origin private file system mounted at the data directory on request.
add_executable(seekdb_wasm_database EXCLUDE_FROM_ALL "${SEEKDB_ROOT}/src/wasm/database_runtime.cpp"
  "${SEEKDB_ROOT}/src/wasm/futex_wait_adapter.cpp"
  "${SEEKDB_ROOT}/src/wasm/wasmfs_adapter.cpp"
  "${SEEKDB_ROOT}/src/wasm/wasmfs_fd_adapter.cpp"
  "${SEEKDB_ROOT}/src/wasm/chunked_memory_backend.cpp")
target_include_directories(seekdb_wasm_database PRIVATE "${EMSCRIPTEN_ROOT_PATH}/system/lib/wasmfs")
get_target_property(seekdb_wasm_database_libraries seekdb_wasm_engine_link_probe LINK_LIBRARIES)
target_link_libraries(seekdb_wasm_database PRIVATE ${seekdb_wasm_database_libraries})
target_compile_definitions(seekdb_wasm_database PRIVATE SEEKDB_WASMFS=1)
set_target_properties(seekdb_wasm_database PROPERTIES SUFFIX ".mjs")
# Keep the deployable JS facade and Worker next to the generated module/Wasm.
# configure_file also makes source updates trigger CMake regeneration.
set(seekdb_wasm_modules database database-worker storage-cleanup-worker worker-server runtime-host mysql-client mysql-transport
  mysql-wire mysql-auth shell shell-sql shell-examples shell-format)
foreach(module ${seekdb_wasm_modules})
  configure_file("${SEEKDB_ROOT}/src/wasm/${module}.mjs" "${CMAKE_CURRENT_BINARY_DIR}/${module}.mjs" COPYONLY)
endforeach()
configure_file("${SEEKDB_ROOT}/src/wasm/engine-version.mjs.in"
  "${CMAKE_CURRENT_BINARY_DIR}/engine-version.mjs" @ONLY)
foreach(asset shell.html shell.css)
  configure_file("${SEEKDB_ROOT}/src/wasm/${asset}" "${CMAKE_CURRENT_BINARY_DIR}/${asset}" COPYONLY)
endforeach()
configure_file("${SEEKDB_ROOT}/unittest/wasm/database-browser.html"
  "${CMAKE_CURRENT_BINARY_DIR}/database-browser.html" COPYONLY)
configure_file("${SEEKDB_ROOT}/src/wasm/database-console.html"
  "${CMAKE_CURRENT_BINARY_DIR}/database-console.html" COPYONLY)
configure_file("${SEEKDB_ROOT}/unittest/wasm/database-browser-cases.mjs"
  "${CMAKE_CURRENT_BINARY_DIR}/database-browser-cases.mjs" COPYONLY)
set(seekdb_wasm_database_link_options
  -Oz --emit-symbol-map -pthread -msimd128 -sUSE_ZLIB=1 -sPROXY_TO_PTHREAD=1
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,worker,node
  -sPTHREAD_POOL_SIZE=64 -sPTHREAD_POOL_SIZE_STRICT=2 -sDEFAULT_PTHREAD_STACK_SIZE=1048576
  -sINITIAL_MEMORY=536870912 -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=2147483648
  -sSTACK_SIZE=2097152 -sASSERTIONS=2 -sEXIT_RUNTIME=1 -Wl,--error-limit=0
  -Wl,--wrap=emscripten_futex_wait
  "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_nio_memory_read','_nio_memory_write','_nio_memory_close']"
  "-sEXPORTED_RUNTIME_METHODS=['HEAPU8','getValue']")
# wasmfs_adapter.cpp wraps these calls to adjust WasmFS behavior and report failures.
set(seekdb_wasmfs_wrapped_calls __syscall_openat __syscall_newfstatat __syscall_stat64 __syscall_lstat64
  __syscall_fstat64 __syscall_mkdirat __syscall_fcntl64 __syscall_renameat __syscall_ftruncate64
  __syscall_truncate64 __syscall_fallocate __syscall_getcwd __syscall_getdents64 __syscall_unlinkat
  __syscall_rmdir __syscall_statfs64 __syscall_fstatfs64 __syscall_fdatasync __syscall_faccessat
  __syscall_chdir __syscall_ioctl __syscall_utimensat __wasi_fd_pread __wasi_fd_pwrite __wasi_fd_sync
  __wasi_fd_seek __wasi_fd_close __syscall_dup3)
set(seekdb_wasmfs_wrap_options)
foreach(call ${seekdb_wasmfs_wrapped_calls})
  list(APPEND seekdb_wasmfs_wrap_options "-Wl,--wrap=${call}")
endforeach()
target_link_options(seekdb_wasm_database PRIVATE ${seekdb_wasm_database_link_options} -sWASMFS
  ${seekdb_wasmfs_wrap_options})

add_executable(test_wasmfs_fd_adapter EXCLUDE_FROM_ALL
  "${SEEKDB_ROOT}/unittest/wasm/test_wasmfs_fd_adapter.cpp"
  "${SEEKDB_ROOT}/src/wasm/wasmfs_fd_adapter.cpp")
target_include_directories(test_wasmfs_fd_adapter PRIVATE "${EMSCRIPTEN_ROOT_PATH}/system/lib/wasmfs")
target_compile_options(test_wasmfs_fd_adapter PRIVATE -UNDEBUG -pthread)
target_link_options(test_wasmfs_fd_adapter PRIVATE -O2 --emit-symbol-map -pthread -sWASMFS
  -sENVIRONMENT=node -sEXIT_RUNTIME=1 -sASSERTIONS=2 -Wl,--wrap=__syscall_dup3)
add_test(NAME wasmfs_fd_adapter COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  $<TARGET_FILE:test_wasmfs_fd_adapter>)
set_tests_properties(wasmfs_fd_adapter PROPERTIES TIMEOUT 20)

# The same engine on the legacy JavaScript file system, in-memory mode only,
# kept for comparison. Serve it with --build-dir build_wasm_engine/memfs.
add_executable(seekdb_wasm_database_memfs EXCLUDE_FROM_ALL "${SEEKDB_ROOT}/src/wasm/database_runtime.cpp"
  "${SEEKDB_ROOT}/src/wasm/futex_wait_adapter.cpp")
target_link_libraries(seekdb_wasm_database_memfs PRIVATE ${seekdb_wasm_database_libraries})
set_target_properties(seekdb_wasm_database_memfs PROPERTIES SUFFIX ".mjs" OUTPUT_NAME seekdb_wasm_database
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/memfs")
foreach(module ${seekdb_wasm_modules})
  configure_file("${SEEKDB_ROOT}/src/wasm/${module}.mjs" "${CMAKE_CURRENT_BINARY_DIR}/memfs/${module}.mjs" COPYONLY)
endforeach()
configure_file("${SEEKDB_ROOT}/src/wasm/engine-version.mjs.in"
  "${CMAKE_CURRENT_BINARY_DIR}/memfs/engine-version.mjs" @ONLY)
configure_file("${SEEKDB_ROOT}/unittest/wasm/database-browser-cases.mjs"
  "${CMAKE_CURRENT_BINARY_DIR}/memfs/database-browser-cases.mjs" COPYONLY)
target_link_options(seekdb_wasm_database_memfs PRIVATE ${seekdb_wasm_database_link_options})

add_executable(test_wasm_log_ring EXCLUDE_FROM_ALL "${SEEKDB_ROOT}/unittest/wasm/test_log_ring.cpp")
target_link_libraries(test_wasm_log_ring PRIVATE ${seekdb_wasm_database_libraries})
target_compile_options(test_wasm_log_ring PRIVATE -UNDEBUG)
target_link_options(test_wasm_log_ring PRIVATE -Oz -pthread -msimd128 -sUSE_ZLIB=1
  -sPROXY_TO_PTHREAD=1 -sPTHREAD_POOL_SIZE=5 -sPTHREAD_POOL_SIZE_STRICT=2
  -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=1048576 -sASSERTIONS=2 -sEXIT_RUNTIME=1)

add_executable(test_wasm_ls_metadata_drain EXCLUDE_FROM_ALL
  "${SEEKDB_ROOT}/unittest/wasm/test_wasm_ls_metadata_drain.cpp")
target_link_libraries(test_wasm_ls_metadata_drain PRIVATE ${seekdb_wasm_database_libraries})
target_compile_options(test_wasm_ls_metadata_drain PRIVATE -UNDEBUG)
target_link_options(test_wasm_ls_metadata_drain PRIVATE -Oz -pthread -msimd128 -sUSE_ZLIB=1
  -Wl,--wrap=_ZN9oceanbase7storage11ObLSService8free_ls_EPNS0_4ObLSE
  -sPROXY_TO_PTHREAD=1 -sPTHREAD_POOL_SIZE=5 -sPTHREAD_POOL_SIZE_STRICT=2
  -sINITIAL_MEMORY=134217728 -sALLOW_MEMORY_GROWTH=0
  -sSTACK_SIZE=1048576 -sASSERTIONS=2 -sEXIT_RUNTIME=1)
add_test(NAME wasm_ls_metadata_drain COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  $<TARGET_FILE:test_wasm_ls_metadata_drain>)
set_tests_properties(wasm_ls_metadata_drain PROPERTIES TIMEOUT 20)

add_executable(test_wasm_stat_key EXCLUDE_FROM_ALL "${SEEKDB_ROOT}/unittest/wasm/test_wasm_stat_key.cpp")
target_link_libraries(test_wasm_stat_key PRIVATE ${seekdb_wasm_database_libraries})
target_compile_options(test_wasm_stat_key PRIVATE -UNDEBUG)
target_link_options(test_wasm_stat_key PRIVATE -Oz -pthread -msimd128 -sUSE_ZLIB=1
  -sPROXY_TO_PTHREAD=1 -sPTHREAD_POOL_SIZE=5 -sPTHREAD_POOL_SIZE_STRICT=2
  -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=1048576 -sASSERTIONS=2 -sEXIT_RUNTIME=1)
add_test(NAME wasm_stat_key COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  $<TARGET_FILE:test_wasm_stat_key>)
set_tests_properties(wasm_stat_key PROPERTIES TIMEOUT 60)

add_executable(test_wasm_hazptr EXCLUDE_FROM_ALL "${SEEKDB_ROOT}/unittest/wasm/test_wasm_hazptr.cpp")
target_link_libraries(test_wasm_hazptr PRIVATE ${seekdb_wasm_database_libraries})
target_compile_options(test_wasm_hazptr PRIVATE -UNDEBUG)
target_link_options(test_wasm_hazptr PRIVATE -Oz -pthread -msimd128 -sUSE_ZLIB=1
  -sPROXY_TO_PTHREAD=1 -sPTHREAD_POOL_SIZE=5 -sPTHREAD_POOL_SIZE_STRICT=2
  -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=1048576 -sASSERTIONS=2 -sEXIT_RUNTIME=1)
add_test(NAME wasm_hazptr COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  $<TARGET_FILE:test_wasm_hazptr>)
set_tests_properties(wasm_hazptr PROPERTIES TIMEOUT 60)

add_executable(test_constraint_recycle_name EXCLUDE_FROM_ALL
  "${SEEKDB_ROOT}/unittest/wasm/test_constraint_recycle_name.cpp")
target_link_libraries(test_constraint_recycle_name PRIVATE ${seekdb_wasm_database_libraries})
target_compile_options(test_constraint_recycle_name PRIVATE -UNDEBUG)
target_link_options(test_constraint_recycle_name PRIVATE -Oz -pthread -msimd128 -sUSE_ZLIB=1
  -sPROXY_TO_PTHREAD=1 -sPTHREAD_POOL_SIZE=5 -sPTHREAD_POOL_SIZE_STRICT=2
  -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=1048576 -sASSERTIONS=2 -sEXIT_RUNTIME=1)
add_test(NAME wasm_constraint_recycle_name COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  $<TARGET_FILE:test_constraint_recycle_name>)
set_tests_properties(wasm_constraint_recycle_name PROPERTIES TIMEOUT 60)

add_executable(test_wasm_ip_distance EXCLUDE_FROM_ALL
  "${SEEKDB_ROOT}/unittest/wasm/test_wasm_ip_distance.cpp")
target_link_libraries(test_wasm_ip_distance PRIVATE ${seekdb_wasm_database_libraries})
target_compile_options(test_wasm_ip_distance PRIVATE -UNDEBUG)
target_link_options(test_wasm_ip_distance PRIVATE -Oz -pthread -msimd128 -sUSE_ZLIB=1
  -sPROXY_TO_PTHREAD=1 -sPTHREAD_POOL_SIZE=5 -sPTHREAD_POOL_SIZE_STRICT=2
  -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=1048576 -sASSERTIONS=2 -sEXIT_RUNTIME=1)
add_test(NAME wasm_ip_distance COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  $<TARGET_FILE:test_wasm_ip_distance>)
set_tests_properties(wasm_ip_distance PROPERTIES TIMEOUT 60)

add_executable(test_wasm_random_range EXCLUDE_FROM_ALL
  "${SEEKDB_ROOT}/unittest/wasm/test_wasm_random_range.cpp")
target_link_libraries(test_wasm_random_range PRIVATE ${seekdb_wasm_database_libraries})
target_compile_options(test_wasm_random_range PRIVATE -UNDEBUG)
target_link_options(test_wasm_random_range PRIVATE -Oz -pthread -msimd128 -sUSE_ZLIB=1
  -sPROXY_TO_PTHREAD=1 -sPTHREAD_POOL_SIZE=5 -sPTHREAD_POOL_SIZE_STRICT=2
  -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=1048576 -sASSERTIONS=2 -sEXIT_RUNTIME=1)
add_test(NAME wasm_random_range COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  $<TARGET_FILE:test_wasm_random_range>)
set_tests_properties(wasm_random_range PROPERTIES TIMEOUT 60)
