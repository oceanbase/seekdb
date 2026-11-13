# Copyright (c) 2025 OceanBase.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Standalone build for the seekdb <-> hipVS bridge.
#
#   cmake -S tools/hipvs_bridge -B build/hipvs_bridge -DCMAKE_PREFIX_PATH=/opt/rocm
#   cmake --build build/hipvs_bridge -j
#
# The bridge is deliberately NOT part of the root seekdb build: the database only
# links against the plain C ABI declared in seekdb_hipvs_bridge.h and dlopen()s the
# shared object at runtime, so a ROCm-free machine keeps building seekdb unchanged.

cmake_minimum_required(VERSION 3.24)
project(seekdb_hipvs_bridge LANGUAGES CXX)

option(SEEKDB_HIPVS_BRIDGE_ENABLE_HIPVS
       "Compile against hipVS/hipRAFT (requires ROCm). When OFF a stub library is built."
       ON)
option(SEEKDB_HIPVS_BRIDGE_BUILD_BENCH
       "Build the googletest-free benchmark/self-check driver"
       ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(seekdb_hipvs_bridge SHARED seekdb_hipvs_bridge.cpp)
target_include_directories(seekdb_hipvs_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# seekdb dlopen()s "libseekdb_cuvs_bridge.so" (same name the Radeon Cloud
# seekdb-hipvs template ships), so keep the file name stable across variants.
set_target_properties(seekdb_hipvs_bridge PROPERTIES
                      OUTPUT_NAME seekdb_cuvs_bridge
                      VERSION 1.0.0
                      SOVERSION 1)

if(SEEKDB_HIPVS_BRIDGE_ENABLE_HIPVS)
  find_package(hipraft CONFIG QUIET)
  find_package(cuvs CONFIG QUIET)
  if(NOT cuvs_FOUND)
    message(FATAL_ERROR
            "hipVS (cuvs) was not found. Point CMAKE_PREFIX_PATH at the hipVS install "
            "(e.g. -DCMAKE_PREFIX_PATH=/opt/rocm;/opt/hipvs), or configure with "
            "-DSEEKDB_HIPVS_BRIDGE_ENABLE_HIPVS=OFF to build the stub.")
  endif()
  target_compile_definitions(seekdb_hipvs_bridge PRIVATE SEEKDB_HAS_HIPVS=1)
  target_link_libraries(seekdb_hipvs_bridge PRIVATE cuvs::cuvs)
  if(TARGET hipraft::hipraft)
    target_link_libraries(seekdb_hipvs_bridge PRIVATE hipraft::hipraft)
  endif()
else()
  message(STATUS "hipVS disabled: building the stub bridge (all calls return UNSUPPORTED)")
endif()

if(SEEKDB_HIPVS_BRIDGE_BUILD_BENCH)
  add_executable(seekdb_hipvs_selfcheck bench/selfcheck.cpp)
  target_link_libraries(seekdb_hipvs_selfcheck PRIVATE seekdb_hipvs_bridge)
endif()

install(TARGETS seekdb_hipvs_bridge LIBRARY DESTINATION lib)
