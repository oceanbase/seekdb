# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20)
project(seekdb_vsag LANGUAGES C CXX)
if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "Use the pinned Emscripten SDK for this dependency build")
endif()
set(CMAKE_CXX_STANDARD 20)
add_compile_options(-pthread -Oz -fexceptions)
include("${CMAKE_CURRENT_LIST_DIR}/blas.cmake")
foreach(option TOOLS UNIT_TESTS MOCK_TESTS BENCHMARKS PKG_CONFIG)
  set(CPUINFO_BUILD_${option} OFF CACHE BOOL "" FORCE)
endforeach()
set(CPUINFO_LIBRARY_TYPE static CACHE STRING "" FORCE)
add_subdirectory("${CPUINFO_SOURCE}" cpuinfo EXCLUDE_FROM_ALL)

# Reuse the pinned upstream source membership, without its host-CPU ISA flags,
# OpenMP flags or -Ofast. Generic kernels establish the numerical baseline.
file(READ "${VSAG_SOURCE}/src/simd/CMakeLists.txt" upstream_simd)
string(REGEX MATCH "set *\\(SIMD_SRCS([^)]*)\\)" source_block "${upstream_simd}")
if(NOT source_block)
  message(FATAL_ERROR "VSAG SIMD source inventory changed")
endif()
string(REGEX MATCHALL "[a-zA-Z0-9_]+\\.cpp" simd_sources "${CMAKE_MATCH_1}")
list(TRANSFORM simd_sources PREPEND "${VSAG_SOURCE}/src/simd/")
add_library(seekdb_vsag_kernels STATIC ${simd_sources})
target_include_directories(seekdb_vsag_kernels PRIVATE
  "${VSAG_SOURCE}/src" "${VSAG_SOURCE}/include" "${CPUINFO_SOURCE}/include")
target_link_libraries(seekdb_vsag_kernels PUBLIC cpuinfo)
install(TARGETS seekdb_vsag_kernels ARCHIVE DESTINATION lib)
install(TARGETS cpuinfo ARCHIVE DESTINATION lib)
install(FILES "${CPUINFO_SOURCE}/include/cpuinfo.h" DESTINATION include)
install(DIRECTORY "${VSAG_SOURCE}/src/simd/" DESTINATION include/seekdb-vsag/simd
  FILES_MATCHING PATTERN "*.h")
install(DIRECTORY "${VSAG_SOURCE}/include/vsag" DESTINATION include)

set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${FMT_SOURCE}" fmt EXCLUDE_FROM_ALL)
# VSAG supports C++17. Use it consistently for fmt and its consumers: fmt
# 10.2.1's C++20 consteval parser fails under the pinned SDK's Clang evaluator.
set_target_properties(fmt PROPERTIES CXX_STANDARD 17)
set(VSAG_VERSION "129b82c-wasm")
configure_file("${VSAG_SOURCE}/src/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h" @ONLY)
add_library(seekdb_vsag_core STATIC
  "${VSAG_SOURCE}/src/vsag.cpp"
  "${VSAG_SOURCE}/src/options.cpp"
  "${VSAG_SOURCE}/src/constants.cpp"
  "${VSAG_SOURCE}/src/dataset_impl.cpp"
  "${VSAG_SOURCE}/src/json_wrapper.cpp"
  "${VSAG_SOURCE}/src/impl/logger/logger.cpp"
  "${VSAG_SOURCE}/src/impl/logger/default_logger.cpp")
set_target_properties(seekdb_vsag_core PROPERTIES CXX_STANDARD 17)
# spdlog is header-only here and shares fmt 10.2.1 with VSAG. This avoids
# embedding a second incompatible fmt namespace from spdlog's bundled copy.
target_compile_definitions(seekdb_vsag_core PRIVATE SPDLOG_FMT_EXTERNAL)
target_include_directories(seekdb_vsag_core PRIVATE
  "${VSAG_SOURCE}/src" "${VSAG_SOURCE}/include" "${CMAKE_CURRENT_BINARY_DIR}"
  "${SPDLOG_SOURCE}/include" "${JSON_SOURCE}/include" "${ROBIN_MAP_SOURCE}/include")
target_link_libraries(seekdb_vsag_core PRIVATE fmt::fmt seekdb_vsag_kernels cpuinfo)
install(TARGETS seekdb_vsag_core fmt ARCHIVE DESTINATION lib)

set(ANTLR_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ANTLR_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ANTLR_BUILD_CPP_TESTS OFF CACHE BOOL "" FORCE)
set(WITH_DEMO OFF CACHE BOOL "" FORCE)
set(ANTLR4_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${ANTLR4_SOURCE}/runtime/Cpp" antlr4 EXCLUDE_FROM_ALL)
set_target_properties(antlr4_static PROPERTIES CXX_STANDARD 17 OUTPUT_NAME seekdb_vsag_antlr)
install(TARGETS antlr4_static ARCHIVE DESTINATION lib)
set(antlr_headers_root "${ANTLR4_SOURCE}/runtime/Cpp/runtime/src")
file(GLOB_RECURSE antlr_headers RELATIVE "${antlr_headers_root}" "${antlr_headers_root}/*.h")
foreach(relative_header IN LISTS antlr_headers)
  set(header "${antlr_headers_root}/${relative_header}")
  # Forward to one canonical file: copying pragma-once headers into a second
  # include root would define ANTLR classes twice in the same translation unit.
  file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/antlr4-runtime/${relative_header}"
    CONTENT "#pragma once\n#include \"@header@\"\n" @ONLY)
endforeach()
file(GLOB parser_headers "${VSAG_SOURCE}/extern/antlr4/fc/*.h")
foreach(header IN LISTS parser_headers)
  get_filename_component(name "${header}" NAME)
  file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/antlr4-autogen/${name}"
    CONTENT "#pragma once\n#include \"@header@\"\n" @ONLY)
endforeach()

# seekdb does not request VSAG's static PQ HNSW variant. Keep this optional
# DiskANN/OpenMP dependency out of the selected HNSW implementation; explicit
# static requests fail with UNSUPPORTED_INDEX instead of silently changing type.
add_compile_definitions(VSAG_DISABLE_STATIC_HNSW VSAG_DISABLE_DISKANN)
# Build the real HNSW implementation and its public Index wrapper while closing
# the dependency graph toward the Factory API used by the seekdb adaptor.
add_library(seekdb_vsag_hnsw STATIC
  "${VSAG_SOURCE}/src/factory/factory.cpp"
  "${VSAG_SOURCE}/src/factory/engine.cpp"
  "${VSAG_SOURCE}/src/factory/resource.cpp"
  "${VSAG_SOURCE}/src/index_common_param.cpp"
  "${VSAG_SOURCE}/src/index/hnsw.cpp"
  "${VSAG_SOURCE}/src/index/hnsw_zparameters.cpp"
  "${VSAG_SOURCE}/src/index_feature_list.cpp"
  "${VSAG_SOURCE}/src/index_detail_info.cpp"
  "${VSAG_SOURCE}/src/impl/label_table.cpp"
  "${VSAG_SOURCE}/src/impl/pruning_strategy.cpp"
  "${VSAG_SOURCE}/src/impl/basic_optimizer.cpp"
  "${VSAG_SOURCE}/src/impl/conjugate_graph.cpp"
  "${VSAG_SOURCE}/src/impl/elias_fano_encoder.cpp"
  "${VSAG_SOURCE}/src/impl/thread_pool/default_thread_pool.cpp"
  "${VSAG_SOURCE}/src/impl/bitset/bitset.cpp"
  "${VSAG_SOURCE}/src/impl/bitset/computable_bitset.cpp"
  "${VSAG_SOURCE}/src/impl/bitset/fast_bitset.cpp"
  "${VSAG_SOURCE}/src/impl/bitset/sparse_bitset.cpp"
  "${VSAG_SOURCE}/src/impl/filter/black_list_filter.cpp"
  "${VSAG_SOURCE}/src/impl/filter/white_list_filter.cpp"
  "${VSAG_SOURCE}/src/impl/filter/extrainfo_wrapper_filter.cpp"
  "${VSAG_SOURCE}/src/impl/odescent/odescent_graph_builder.cpp"
  "${VSAG_SOURCE}/src/impl/odescent/odescent_graph_parameter.cpp"
  "${VSAG_SOURCE}/src/utils/util_functions.cpp"
  "${VSAG_SOURCE}/src/utils/sparse_vector_transform.cpp"
  "${VSAG_SOURCE}/src/utils/slow_task_timer.cpp"
  "${VSAG_SOURCE}/src/utils/timer.cpp"
  "${VSAG_SOURCE}/src/utils/window_result_queue.cpp"
  "${VSAG_SOURCE}/src/storage/footer.cpp"
  "${VSAG_SOURCE}/src/storage/serialization.cpp"
  "${VSAG_SOURCE}/src/algorithm/hnswlib/hnswalg.cpp"
  "${VSAG_SOURCE}/src/algorithm/hnswlib/block_manager.cpp"
  "${VSAG_SOURCE}/src/algorithm/hnswlib/algorithm_interface.cpp"
  "${VSAG_SOURCE}/src/impl/allocator/default_allocator.cpp"
  "${VSAG_SOURCE}/src/utils/lock_strategy.cpp"
  "${VSAG_SOURCE}/src/utils/prefetch.cpp"
  "${VSAG_SOURCE}/src/utils/linear_congruential_generator.cpp"
  "${VSAG_SOURCE}/src/utils/visited_list.cpp"
  "${VSAG_SOURCE}/src/index/iterator_filter.cpp"
  "${VSAG_SOURCE}/src/storage/stream_reader.cpp"
  "${VSAG_SOURCE}/src/storage/stream_writer.cpp")
set_target_properties(seekdb_vsag_hnsw PROPERTIES CXX_STANDARD 17)
target_compile_definitions(seekdb_vsag_hnsw PRIVATE SPDLOG_FMT_EXTERNAL)
target_include_directories(seekdb_vsag_hnsw PUBLIC
  "${VSAG_SOURCE}/src" "${VSAG_SOURCE}/include"
  "${CPUINFO_SOURCE}/include" "${JSON_SOURCE}/include" "${ROBIN_MAP_SOURCE}/include"
  "${THREAD_POOL_SOURCE}"
  "${CMAKE_INSTALL_PREFIX}/include" "${CMAKE_INSTALL_PREFIX}/include/roaring")
target_include_directories(seekdb_vsag_hnsw PRIVATE "${SPDLOG_SOURCE}/include"
  "${VSAG_SOURCE}/extern/antlr4/fc" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(seekdb_vsag_hnsw PUBLIC seekdb_vsag_core fmt::fmt seekdb_vsag_blas antlr4_static)
# Follow upstream's explicit inventories for the quantizer and I/O closures.
# These are used by HNSW's public build/serialization paths and HGraph.
function(vsag_add_inventory directory variable)
  file(READ "${VSAG_SOURCE}/src/${directory}/CMakeLists.txt" upstream)
  string(REGEX MATCH "set *\\(${variable}([^)]*)\\)" block "${upstream}")
  if(NOT block)
    message(FATAL_ERROR "VSAG ${directory} source inventory changed")
  endif()
  string(REGEX MATCHALL "[a-zA-Z0-9_/]+\\.cpp" sources "${CMAKE_MATCH_1}")
  list(TRANSFORM sources PREPEND "${VSAG_SOURCE}/src/${directory}/")
  target_sources(seekdb_vsag_hnsw PRIVATE ${sources})
endfunction()
vsag_add_inventory(io IO_SRC)
vsag_add_inventory(quantization QUANTIZER_SRC)
vsag_add_inventory(impl/transform TRANSFORM_SRC)
vsag_add_inventory(impl/heap HEAP_SRC)
vsag_add_inventory(impl/searcher SEARCHER_SRC)
vsag_add_inventory(impl/reorder REORDER_SRC)
vsag_add_inventory(algorithm/ivf_partition IVF_PARTITION_SRCS)
vsag_add_inventory(algorithm/sindi SINDI_SRCS)
target_compile_definitions(seekdb_vsag_hnsw PUBLIC HAVE_LIBAIO=0)
# Reuse the upstream non-test datacell/executor and generated-parser inventories.
file(GLOB data_sources "${VSAG_SOURCE}/src/datacell/*.cpp")
file(GLOB executor_sources "${VSAG_SOURCE}/src/attr/executor/*.cpp")
file(GLOB parser_sources "${VSAG_SOURCE}/extern/antlr4/fc/*.cpp")
file(GLOB algorithm_sources "${VSAG_SOURCE}/src/algorithm/*.cpp")
list(FILTER data_sources EXCLUDE REGEX "_test.cpp$")
list(FILTER executor_sources EXCLUDE REGEX "_test.cpp$")
list(FILTER algorithm_sources EXCLUDE REGEX "_test.cpp$")
target_sources(seekdb_vsag_hnsw PRIVATE
  ${data_sources} ${executor_sources} ${parser_sources} ${algorithm_sources}
  "${VSAG_SOURCE}/src/attr/expression_visitor.cpp"
  "${VSAG_SOURCE}/src/attr/attribute.cpp"
  "${VSAG_SOURCE}/src/attr/attr_type_schema.cpp"
  "${VSAG_SOURCE}/src/attr/multi_bitset_manager.cpp"
  "${VSAG_SOURCE}/src/attr/attr_value_map.cpp"
  "${VSAG_SOURCE}/src/attr/argparse.cpp"
  "${VSAG_SOURCE}/src/impl/cluster/kmeans_cluster.cpp")
install(TARGETS seekdb_vsag_hnsw ARCHIVE DESTINATION lib)
# Internal headers are installed in their own include root for the HNSW probes.
# The public engine include/vsag ABI remains the original upstream interface.
install(DIRECTORY "${VSAG_SOURCE}/src/" DESTINATION include/seekdb-vsag
  FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
install(DIRECTORY "${FMT_SOURCE}/include/fmt" "${JSON_SOURCE}/include/nlohmann"
  "${ROBIN_MAP_SOURCE}/include/tsl" DESTINATION include/seekdb-vsag)
install(FILES "${THREAD_POOL_SOURCE}/ThreadPool.h" DESTINATION include/seekdb-vsag)
