# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
#
# Standalone CMake project for the optional AMD GPU HNSW backend of seekdb.
#
# It is NOT part of the seekdb build: the server binary is produced by the
# regular (CPU only) seekdb toolchain, and this project produces a separate
# shared object that the server dlopen()s at runtime. Keeping the two build
# systems apart is what keeps ROCm out of the database link line.
#
# Usage:
#   cmake -S . -B build -G Ninja \
#         -DCMAKE_CXX_COMPILER=hipcc \
#         -DCMAKE_HIP_ARCHITECTURES=gfx1100 \
#         -DCMAKE_PREFIX_PATH="/opt/rocm/lib/cmake;/opt/hipvs" \
#         -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#   cmake --build build --target install     # -> <prefix>/lib/libseekdb_hipvs.so
cmake_minimum_required(VERSION 3.31)
project(seekdb_hipvs_backend LANGUAGES CXX HIP)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT DEFINED CMAKE_HIP_ARCHITECTURES)
  set(CMAKE_HIP_ARCHITECTURES "gfx1100")
endif()

find_package(hip REQUIRED)
# hipVS installs a CMake package that keeps the upstream `cuvs` namespace.
find_package(cuvs REQUIRED)

add_library(seekdb_hipvs SHARED hipvs_build.cpp)
target_include_directories(seekdb_hipvs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(seekdb_hipvs PRIVATE cuvs::cuvs)

# The snapshot converter is deliberately plain host C++: only hipvs_build.cpp
# contains device code, so a broken GPU toolchain cannot silently degrade the
# conversion step.
set_source_files_properties(hipvs_build.cpp PROPERTIES LANGUAGE HIP)

target_compile_options(seekdb_hipvs PRIVATE -O3 -fvisibility=hidden)
target_link_options(seekdb_hipvs PRIVATE -Wl,--no-undefined)

install(TARGETS seekdb_hipvs
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin)

# Optional acceptance/benchmark helper (never shipped into the server):
#   cmake --build build --target seekdb_hipvs_selftest
add_executable(seekdb_hipvs_selftest EXCLUDE_FROM_ALL tools/hipvs_selftest.cpp)
set_source_files_properties(tools/hipvs_selftest.cpp PROPERTIES LANGUAGE CXX)
target_include_directories(seekdb_hipvs_selftest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(seekdb_hipvs_selftest PRIVATE seekdb_hipvs hnswlib::hnswlib)
target_compile_options(seekdb_hipvs_selftest PRIVATE -O3)

# Diagnostic: prove that VSAG (the library seekdb uses for lib=vsag) can import
# the GPU-built snapshot and answer queries. Needs the seekdb third-party VSAG;
# pass -DVSAG_ROOT=<dir containing include/vsag and lib/vsag_lib/libvsag.so>.
#   cmake --build build --target vsag_import_probe
set(VSAG_ROOT "" CACHE PATH "seekdb third-party devel dir that contains vsag")
if(VSAG_ROOT AND EXISTS "${VSAG_ROOT}/include/vsag/vsag.h")
  add_executable(vsag_import_probe EXCLUDE_FROM_ALL tools/vsag_import_probe.cpp)
  set_source_files_properties(tools/vsag_import_probe.cpp PROPERTIES LANGUAGE CXX)
  target_include_directories(vsag_import_probe PRIVATE "${VSAG_ROOT}/include")
  target_link_directories(vsag_import_probe PRIVATE "${VSAG_ROOT}/lib/vsag_lib")
  target_link_libraries(vsag_import_probe PRIVATE vsag)
  target_compile_options(vsag_import_probe PRIVATE -O2)
  message(STATUS "seekdb_hipvs: vsag_import_probe enabled (VSAG_ROOT=${VSAG_ROOT})")
else()
  message(STATUS "seekdb_hipvs: vsag_import_probe disabled (-DVSAG_ROOT not set)")
endif()