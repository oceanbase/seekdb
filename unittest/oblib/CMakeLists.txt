# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0.

seekdb_add_module_tests(oblib_tests
  lib/alloc/test_jemalloc_hook.cpp
  lib/allocator/test_allocator_facade.cpp
  lib/resource/test_memory_quota.cpp)

seekdb_add_module_benchmark(memory_quota_benchmark
  "${CMAKE_SOURCE_DIR}/tools/testing/memory_quota_benchmark.cpp"
  "${CMAKE_SOURCE_DIR}/src/oblib/lib/thread_local/ob_tsi_utils.cpp")
target_include_directories(memory_quota_benchmark PRIVATE
  "${CMAKE_SOURCE_DIR}/src/oblib/easy"
  "${CMAKE_SOURCE_DIR}/src/oblib/easy/include")
target_compile_definitions(memory_quota_benchmark PRIVATE
  SEEKDB_STANDALONE_QUOTA_BENCHMARK=1)
