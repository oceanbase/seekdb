# Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
set(SEEKDB_WASM_RUST_DIR "${SEEKDB_ROOT}/build_wasm_rust_runtime" CACHE PATH
  "Directory built by tools/wasm/build-rust-nio.sh")
set(SEEKDB_WASM_NIO_ARCHIVE
  "${SEEKDB_WASM_RUST_DIR}/target/wasm32-unknown-emscripten/release/libsql_nio.a")
if(NOT EXISTS "${SEEKDB_WASM_NIO_ARCHIVE}")
  message(FATAL_ERROR "Build Rust memory NIO with tools/wasm/build-rust-nio.sh ${SEEKDB_WASM_RUST_DIR}")
endif()
add_library(seekdb_wasm_nio STATIC IMPORTED GLOBAL)
add_custom_target(seekdb_wasm_nio_check
  COMMAND "${Python3_EXECUTABLE}" "${SEEKDB_ROOT}/tools/wasm/rust-nio-manifest.py"
    "${SEEKDB_WASM_RUST_DIR}"
  VERBATIM)
add_dependencies(seekdb_wasm_nio seekdb_wasm_nio_check)
set_target_properties(seekdb_wasm_nio PROPERTIES
  IMPORTED_LOCATION "${SEEKDB_WASM_NIO_ARCHIVE}"
  INTERFACE_INCLUDE_DIRECTORIES "${SEEKDB_ROOT}/rust/sql-nio/include")
target_link_libraries(seekdb_wasm_engine_link_probe PRIVATE seekdb_wasm_nio)

add_executable(test_wasm_nio_memory "${SEEKDB_ROOT}/unittest/wasm/test_wasm_nio_memory.cpp")
target_link_libraries(test_wasm_nio_memory PRIVATE seekdb_wasm_nio)
target_compile_features(test_wasm_nio_memory PRIVATE cxx_std_20)
target_compile_options(test_wasm_nio_memory PRIVATE -pthread -Oz -UNDEBUG)
target_link_options(test_wasm_nio_memory PRIVATE -pthread -Oz -sPROXY_TO_PTHREAD=1
  -sPTHREAD_POOL_SIZE=6 -sPTHREAD_POOL_SIZE_STRICT=2 -sINITIAL_MEMORY=67108864
  -sSTACK_SIZE=1048576 -sASSERTIONS=2 -sEXIT_RUNTIME=1 -Wl,--fatal-warnings)
add_test(NAME wasm_nio_memory COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  "$<TARGET_FILE:test_wasm_nio_memory>")
set_tests_properties(wasm_nio_memory PROPERTIES TIMEOUT 45)
