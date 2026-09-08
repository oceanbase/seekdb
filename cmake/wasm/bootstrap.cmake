# Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
# Development lifecycle experiment with an explicit, large pthread/memory budget.
add_executable(seekdb_wasm_bootstrap_probe EXCLUDE_FROM_ALL
  "${SEEKDB_ROOT}/unittest/wasm/engine_bootstrap_probe.cpp")
get_target_property(seekdb_wasm_lifecycle_libraries seekdb_wasm_engine_link_probe LINK_LIBRARIES)
target_link_libraries(seekdb_wasm_bootstrap_probe PRIVATE ${seekdb_wasm_lifecycle_libraries})
target_link_options(seekdb_wasm_bootstrap_probe PRIVATE
  -Oz --emit-symbol-map -pthread -msimd128 -sUSE_ZLIB=1 -sPROXY_TO_PTHREAD=1
  -sPTHREAD_POOL_SIZE=64 -sPTHREAD_POOL_SIZE_STRICT=2 -sDEFAULT_PTHREAD_STACK_SIZE=1048576
  -sINITIAL_MEMORY=536870912 -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=2147483648
  -sSTACK_SIZE=2097152 -sASSERTIONS=2 -sEXIT_RUNTIME=1 -Wl,--error-limit=0)
target_link_options(seekdb_wasm_bootstrap_probe PRIVATE
  "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_nio_memory_read','_nio_memory_write','_nio_memory_close']"
  "-sEXPORTED_RUNTIME_METHODS=['HEAPU8','getValue']")
