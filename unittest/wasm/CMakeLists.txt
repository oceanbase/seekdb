cmake_minimum_required(VERSION 3.20)
project(seekdb_wasm_platform_tests LANGUAGES C CXX)

option(SEEKDB_NATIVE_PLATFORM_REFERENCE "Run the shared tests against the native platform branches" OFF)
if(NOT EMSCRIPTEN AND NOT SEEKDB_NATIVE_PLATFORM_REFERENCE)
  message(FATAL_ERROR "Configure with emcmake cmake; these tests require real WebAssembly")
endif()

enable_testing()
# Match the native OBLib contract in bazel/seekdb_build_config.bzl. The legacy
# exception-based lock classes are not part of seekdb's supported build.
add_compile_definitions(_NO_EXCEPTION __STDC_LIMIT_MACROS __STDC_CONSTANT_MACROS)
add_compile_options(-fno-strict-aliasing)
configure_file(runner.html runner.html COPYONLY)
get_filename_component(SEEKDB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(test_wasm_atomics test_wasm_atomics.cpp atomic_peer.cpp)
target_compile_features(test_wasm_atomics PRIVATE cxx_std_20)
target_include_directories(test_wasm_atomics PRIVATE "${SEEKDB_ROOT}/src/oblib")
target_include_directories(test_wasm_atomics PRIVATE "${SEEKDB_ROOT}/src/oblib/easy/include")
target_compile_options(test_wasm_atomics PRIVATE -pthread -UNDEBUG)
if(EMSCRIPTEN)
target_link_options(test_wasm_atomics PRIVATE
  -pthread -sPTHREAD_POOL_SIZE=4 -sPTHREAD_POOL_SIZE_STRICT=2
  -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSTACK_SIZE=1048576 -sABORTING_MALLOC=0)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(test_wasm_atomics PRIVATE Threads::Threads)
endif()
add_test(NAME wasm_atomics COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  "$<TARGET_FILE:test_wasm_atomics>")
set_tests_properties(wasm_atomics PROPERTIES TIMEOUT 60)

# Compile the real allocator translation units as a separate gate. This does
# not establish that the complete memory runtime links or starts yet.
if(EMSCRIPTEN)
include("${SEEKDB_ROOT}/cmake/wasm/runtime.cmake")
add_executable(test_wasm_io test_wasm_io.cpp)
target_compile_features(test_wasm_io PRIVATE cxx_std_20)
target_compile_options(test_wasm_io PRIVATE -pthread -UNDEBUG)
target_include_directories(test_wasm_io PRIVATE "${SEEKDB_ROOT}/src" "${SEEKDB_ROOT}/src/oblib")
# Keep the JS runtime event loop available for filesystem proxy calls while
# the C++ entry point joins I/O workers or waits for completion notifications.
target_link_options(test_wasm_io PRIVATE
  -pthread -sPROXY_TO_PTHREAD=1 -sPTHREAD_POOL_SIZE=4 -sPTHREAD_POOL_SIZE_STRICT=2
  -sABORTING_MALLOC=0 -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSTACK_SIZE=1048576
  -Wl,--wrap=pwrite)
add_test(NAME wasm_io COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} "$<TARGET_FILE:test_wasm_io>")
set_tests_properties(wasm_io PROPERTIES TIMEOUT 30)

add_executable(test_wasm_memory_runtime test_wasm_memory_runtime.cpp)
target_link_libraries(test_wasm_memory_runtime PRIVATE seekdb_wasm_memory seekdb_wasm_runtime_support)
target_compile_features(test_wasm_memory_runtime PRIVATE cxx_std_20)
target_compile_options(test_wasm_memory_runtime PRIVATE -pthread -UNDEBUG)
target_include_directories(test_wasm_memory_runtime PRIVATE
  "${SEEKDB_ROOT}/src/oblib" "${SEEKDB_ROOT}/src/oblib/easy"
  "${SEEKDB_ROOT}/src/oblib/easy/include" "${SEEKDB_ROOT}/src" "${SEEKDB_ROOT}/include")
target_link_options(test_wasm_memory_runtime PRIVATE
  -pthread -sUSE_ZLIB=1 -sPTHREAD_POOL_SIZE=4 -sPTHREAD_POOL_SIZE_STRICT=2 -sABORTING_MALLOC=0
  -sINITIAL_MEMORY=134217728 -sSTACK_SIZE=1048576 -sASSERTIONS=2 -sEXIT_RUNTIME=1
  -sSTACK_OVERFLOW_CHECK=2 -Wl,--error-limit=0)
add_test(NAME wasm_memory_runtime COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  "$<TARGET_FILE:test_wasm_memory_runtime>")
set_tests_properties(wasm_memory_runtime PROPERTIES TIMEOUT 60)

add_executable(test_wasm_stack test_wasm_stack.cpp
  "${SEEKDB_ROOT}/src/oblib/lib/utility/ob_common_utility.cpp"
  "${SEEKDB_ROOT}/src/oblib/lib/utility/ob_backtrace.cpp")
target_compile_features(test_wasm_stack PRIVATE cxx_std_20)
target_compile_options(test_wasm_stack PRIVATE -pthread -UNDEBUG)
target_include_directories(test_wasm_stack PRIVATE
  "${SEEKDB_ROOT}/src/oblib" "${SEEKDB_ROOT}/src/oblib/easy"
  "${SEEKDB_ROOT}/src/oblib/easy/include" "${SEEKDB_ROOT}/src" "${SEEKDB_ROOT}/include")
target_link_options(test_wasm_stack PRIVATE
  -pthread -sPTHREAD_POOL_SIZE=1 -sPTHREAD_POOL_SIZE_STRICT=2
  -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSTACK_OVERFLOW_CHECK=2 -sSTACK_SIZE=1048576)
add_test(NAME wasm_stack COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  "$<TARGET_FILE:test_wasm_stack>")
set_tests_properties(wasm_stack PROPERTIES TIMEOUT 60)

add_executable(test_wasm_futex test_wasm_futex.cpp
  "${SEEKDB_ROOT}/src/oblib/lib/lock/ob_futex.cpp")
target_compile_features(test_wasm_futex PRIVATE cxx_std_20)
target_compile_options(test_wasm_futex PRIVATE -pthread -UNDEBUG)
target_include_directories(test_wasm_futex PRIVATE
  "${SEEKDB_ROOT}/src/oblib" "${SEEKDB_ROOT}/src/oblib/easy/include")
target_link_options(test_wasm_futex PRIVATE
  -pthread -sPTHREAD_POOL_SIZE=3 -sPTHREAD_POOL_SIZE_STRICT=2
  -sEXIT_RUNTIME=1 -sASSERTIONS=2 -sSTACK_SIZE=1048576)
add_test(NAME wasm_futex COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
  "$<TARGET_FILE:test_wasm_futex>")
set_tests_properties(wasm_futex PROPERTIES TIMEOUT 30)

add_library(test_wasm_allocator_compile OBJECT
  "${SEEKDB_ROOT}/src/oblib/lib/resource/achunk_mgr.cpp"
  "${SEEKDB_ROOT}/src/oblib/lib/alloc/block_set.cpp")
target_compile_features(test_wasm_allocator_compile PRIVATE cxx_std_20)
target_compile_options(test_wasm_allocator_compile PRIVATE -pthread)
target_include_directories(test_wasm_allocator_compile PRIVATE
  "${SEEKDB_ROOT}/src/oblib" "${SEEKDB_ROOT}/src/oblib/easy"
  "${SEEKDB_ROOT}/src/oblib/easy/include" "${SEEKDB_ROOT}/src" "${SEEKDB_ROOT}/include")
endif()
