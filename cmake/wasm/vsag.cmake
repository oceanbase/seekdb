# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
foreach(library vsag_kernels vsag_core vsag_hnsw vsag_blas vsag_antlr cpuinfo fmt)
  set(archive "${library}")
  if(library MATCHES "^vsag_")
    set(archive "seekdb_${library}")
  endif()
  if(NOT EXISTS "${SEEKDB_WASM_DEPS}/lib/lib${archive}.a")
    message(FATAL_ERROR "Build VSAG with tools/wasm/build-vsag.sh")
  endif()
  add_library(seekdb_wasm_${library} STATIC IMPORTED GLOBAL)
  set_target_properties(seekdb_wasm_${library} PROPERTIES
    IMPORTED_LOCATION "${SEEKDB_WASM_DEPS}/lib/lib${archive}.a"
    INTERFACE_INCLUDE_DIRECTORIES "${SEEKDB_WASM_DEPS}/include;${SEEKDB_WASM_DEPS}/include/seekdb-vsag")
endforeach()
target_link_libraries(seekdb_wasm_vsag_kernels INTERFACE seekdb_wasm_cpuinfo)
target_link_libraries(seekdb_wasm_vsag_core INTERFACE seekdb_wasm_vsag_kernels seekdb_wasm_fmt)
target_link_libraries(seekdb_wasm_vsag_hnsw INTERFACE seekdb_wasm_vsag_core seekdb_wasm_roaring seekdb_wasm_vsag_blas seekdb_wasm_vsag_antlr)
target_include_directories(seekdb_wasm_vsag_hnsw INTERFACE "${SEEKDB_WASM_DEPS}/include/roaring")
target_compile_definitions(seekdb_wasm_vsag_hnsw INTERFACE VSAG_DISABLE_STATIC_HNSW HAVE_LIBAIO=0)
target_link_libraries(seekdb_wasm_engine_link_probe PRIVATE seekdb_wasm_vsag_hnsw)
# This validates the actual VSAG distance module separately from the public
# index Factory test below. SQL adapter/browser integration is still required.
add_executable(test_wasm_vector_distance "${SEEKDB_ROOT}/unittest/wasm/test_wasm_vector_distance.cpp")
target_link_libraries(test_wasm_vector_distance PRIVATE seekdb_wasm_vsag_kernels)
target_compile_features(test_wasm_vector_distance PRIVATE cxx_std_20)
target_compile_options(test_wasm_vector_distance PRIVATE -pthread -Oz -fexceptions -UNDEBUG)
target_link_options(test_wasm_vector_distance PRIVATE -pthread -Oz -fexceptions
  -sASSERTIONS=2 -sEXIT_RUNTIME=1 -sPTHREAD_POOL_SIZE=3)
add_test(NAME wasm_vector_distance COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  "$<TARGET_FILE:test_wasm_vector_distance>")
set_tests_properties(wasm_vector_distance PROPERTIES TIMEOUT 45)

add_executable(test_wasm_vsag_runtime "${SEEKDB_ROOT}/unittest/wasm/test_wasm_vsag_runtime.cpp")
target_link_libraries(test_wasm_vsag_runtime PRIVATE seekdb_wasm_vsag_core)
target_compile_features(test_wasm_vsag_runtime PRIVATE cxx_std_20)
target_compile_options(test_wasm_vsag_runtime PRIVATE -pthread -Oz -fexceptions -UNDEBUG)
target_link_options(test_wasm_vsag_runtime PRIVATE -pthread -Oz -fexceptions
  -sASSERTIONS=2 -sEXIT_RUNTIME=1 -sPTHREAD_POOL_SIZE=3)
add_test(NAME wasm_vsag_runtime COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  "$<TARGET_FILE:test_wasm_vsag_runtime>")
set_tests_properties(wasm_vsag_runtime PROPERTIES TIMEOUT 45)

foreach(probe hnsw vsag_stream vsag_index_support vsag_blas)
  add_executable(test_wasm_${probe} "${SEEKDB_ROOT}/unittest/wasm/test_wasm_${probe}.cpp")
  target_link_libraries(test_wasm_${probe} PRIVATE seekdb_wasm_vsag_hnsw)
  target_compile_features(test_wasm_${probe} PRIVATE cxx_std_17)
  target_compile_options(test_wasm_${probe} PRIVATE -pthread -Oz -UNDEBUG)
  target_link_options(test_wasm_${probe} PRIVATE -pthread -Oz -sASSERTIONS=2 -sEXIT_RUNTIME=1
    -sPTHREAD_POOL_SIZE=3 -sSTACK_SIZE=1048576)
  add_test(NAME wasm_${probe} COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
    "$<TARGET_FILE:test_wasm_${probe}>")
  set_tests_properties(wasm_${probe} PROPERTIES TIMEOUT 45)
endforeach()
target_sources(test_wasm_vsag_stream PRIVATE
  "${SEEKDB_ROOT}/unittest/wasm/vsag_default_allocator_fixture.cpp")
target_sources(test_wasm_vsag_blas PRIVATE
  "${SEEKDB_ROOT}/unittest/wasm/blas_malloc_fixture.cpp")
target_link_options(test_wasm_vsag_blas PRIVATE -Wl,--wrap=malloc -Wl,--fatal-warnings)

add_executable(test_wasm_vsag_factory "${SEEKDB_ROOT}/unittest/wasm/test_wasm_vsag_factory.cpp")
target_link_libraries(test_wasm_vsag_factory PRIVATE seekdb_wasm_vsag_hnsw)
target_compile_features(test_wasm_vsag_factory PRIVATE cxx_std_17)
target_compile_options(test_wasm_vsag_factory PRIVATE -pthread -Oz -UNDEBUG)
target_link_options(test_wasm_vsag_factory PRIVATE -pthread -Oz -sASSERTIONS=2 -sEXIT_RUNTIME=1
  -sPTHREAD_POOL_SIZE=6 -sPTHREAD_POOL_SIZE_STRICT=2 -sSTACK_SIZE=1048576
  -sINITIAL_MEMORY=67108864 -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=536870912
  -Wl,--error-limit=0 -Wl,--fatal-warnings)
add_test(NAME wasm_vsag_factory COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  "$<TARGET_FILE:test_wasm_vsag_factory>")
set_tests_properties(wasm_vsag_factory PROPERTIES TIMEOUT 60)

# Explicit opt-in strict link probe for the public Index implementation. The
# Factory CTest above exercises the broader query/mutation/restore behavior.
add_executable(seekdb_wasm_vsag_index_link_probe EXCLUDE_FROM_ALL
  "${SEEKDB_ROOT}/unittest/wasm/vsag_index_link_probe.cpp")
target_link_libraries(seekdb_wasm_vsag_index_link_probe PRIVATE seekdb_wasm_vsag_hnsw)
target_compile_features(seekdb_wasm_vsag_index_link_probe PRIVATE cxx_std_17)
target_compile_options(seekdb_wasm_vsag_index_link_probe PRIVATE -pthread -Oz -UNDEBUG)
target_link_options(seekdb_wasm_vsag_index_link_probe PRIVATE -pthread -Oz
  -sASSERTIONS=2 -sEXIT_RUNTIME=1 -sPTHREAD_POOL_SIZE=3 -sSTACK_SIZE=1048576
  -Wl,--error-limit=0)
