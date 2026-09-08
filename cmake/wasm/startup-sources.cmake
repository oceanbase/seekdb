# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
include("${SEEKDB_ROOT}/cmake/generate_syspack.cmake")
seekdb_generate_syspack("${SEEKDB_ROOT}"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/syspack"
  "${CMAKE_CURRENT_BINARY_DIR}/syspack_release" wasm_syspack_source)

execute_process(COMMAND git -C "${SEEKDB_ROOT}" rev-parse HEAD
  OUTPUT_VARIABLE GIT_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND git -C "${SEEKDB_ROOT}" rev-parse --abbrev-ref HEAD
  OUTPUT_VARIABLE GIT_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
set(BUILD_NUMBER 1)
set(BUILD_FLAGS "${CMAKE_BUILD_TYPE}|wasm32|pthread")
file(STRINGS "${SEEKDB_ROOT}/tools/wasm/emscripten-version" wasm_sdk_version)
set(BUILD_INFO "Emscripten ${wasm_sdk_version}; Clang ${CMAKE_CXX_COMPILER_VERSION}")
configure_file("${SEEKDB_ROOT}/src/share/ob_version.cpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/ob_version.cpp" @ONLY)
add_library(seekdb_wasm_startup_sources STATIC EXCLUDE_FROM_ALL
  "${wasm_syspack_source}" "${CMAKE_CURRENT_BINARY_DIR}/generated/ob_version.cpp"
  # Exactly the production OB_ENABLE_STANDBY=OFF source membership.
  "${SEEKDB_ROOT}/src/standby/standby_module_disabled.cpp"
  "${SEEKDB_ROOT}/src/standby/control/standby_state_store.cpp")
target_link_libraries(seekdb_wasm_startup_sources PRIVATE seekdb_wasm_engine_options)
add_dependencies(seekdb_wasm_engine_objects seekdb_wasm_startup_sources)
