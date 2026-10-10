cmake_minimum_required(VERSION 3.20)
project(vsag_ios LANGUAGES C CXX)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  message(FATAL_ERROR "This VSAG adapter requires an iOS toolchain")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Import only CBLAS declarations; the Accelerate umbrella also declares Fortran
# LAPACK entry points with signatures that conflict with LAPACKE's headers.
add_compile_options("SHELL:-iframework ${CMAKE_OSX_SYSROOT}/System/Library/Frameworks/Accelerate.framework/Frameworks")
set(ENABLE_TESTS OFF)
set(ENABLE_INTEL_MKL OFF)
set(HAVE_LIBAIO 0)
add_compile_definitions(HAVE_LIBAIO=0 NO_LIBAIO=1 ENABLE_CUSTOM_LOGGER=1)
include_directories(include src "${SEEKDB_PREFIX}/include"
  "${SEEKDB_BOOST_INCLUDE}" "${SEEKDB_ADAPTER}/include"
  "${SEEKDB_SPDLOG}/include" "${SEEKDB_JSON}/include"
  "${SEEKDB_FMT}/include"
  "${SEEKDB_TSL}/include" "${SEEKDB_THREAD_POOL}"
  "${SEEKDB_CPUINFO}/include" "${SEEKDB_ANTLR4}/runtime/Cpp/runtime/src"
  extern/antlr4/fc)
add_library(coverage_config INTERFACE)
target_compile_definitions(coverage_config INTERFACE BOOST_NO_CXX98_FUNCTION_BASE)
# Symlinks retain header identity for upstream files guarded with pragma once.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/include")
file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/include/antlr4-autogen"
  "${CMAKE_BINARY_DIR}/include/antlr4-runtime")
file(CREATE_LINK "${CMAKE_CURRENT_SOURCE_DIR}/extern/antlr4/fc"
  "${CMAKE_BINARY_DIR}/include/antlr4-autogen" SYMBOLIC)
file(CREATE_LINK "${SEEKDB_ANTLR4}/runtime/Cpp/runtime/src"
  "${CMAKE_BINARY_DIR}/include/antlr4-runtime" SYMBOLIC)
target_include_directories(coverage_config INTERFACE "${CMAKE_BINARY_DIR}/include")

# Preserve the upstream source target graph without downloading host binaries.
macro(maybe_add_dependencies depender)
  if(TARGET ${depender})
    foreach(dependee ${ARGN})
      if(TARGET ${dependee})
        add_dependencies(${depender} ${dependee})
      endif()
    endforeach()
  endif()
endmacro()
add_library(boost INTERFACE)
add_library(openblas INTERFACE)
add_library(spdlog INTERFACE)
add_library(nlohmann_json INTERFACE)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
add_library(roaring STATIC IMPORTED)
set_target_properties(roaring PROPERTIES
  IMPORTED_LOCATION "${SEEKDB_PREFIX}/lib/libroaring.a"
  INTERFACE_INCLUDE_DIRECTORIES "${SEEKDB_PREFIX}/include/roaring")
add_library(omp STATIC IMPORTED)
set_target_properties(omp PROPERTIES IMPORTED_LOCATION "${SEEKDB_PREFIX}/lib/libomp.a")
add_library(lapacke STATIC IMPORTED)
set_target_properties(lapacke PROPERTIES IMPORTED_LOCATION "${SEEKDB_PREFIX}/lib/liblapacke.a")
set(BLAS_LIBRARIES lapacke "-framework Accelerate" omp)

set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${SEEKDB_FMT}" "${CMAKE_BINARY_DIR}/fmt")
foreach(option BUILD_TOOLS BUILD_UNIT_TESTS BUILD_MOCK_TESTS BUILD_BENCHMARKS BUILD_PKG_CONFIG)
  set(CPUINFO_${option} OFF CACHE BOOL "" FORCE)
endforeach()
set(CPUINFO_LIBRARY_TYPE static CACHE STRING "" FORCE)
add_subdirectory("${SEEKDB_CPUINFO}" "${CMAKE_BINARY_DIR}/cpuinfo")

# ANTLR's checked-in C++ runtime needs no host Java generator.
file(GLOB_RECURSE _antlr_sources CONFIGURE_DEPENDS "${SEEKDB_ANTLR4}/runtime/Cpp/runtime/src/*.cpp")
add_library(antlr4-runtime STATIC ${_antlr_sources})
target_compile_definitions(antlr4-runtime PUBLIC ANTLR4CPP_STATIC)
add_library(antlr4 INTERFACE)
file(GLOB _generated_parser CONFIGURE_DEPENDS "extern/antlr4/fc/*.cpp")
add_library(antlr4-autogen STATIC ${_generated_parser})
target_link_libraries(antlr4-autogen PUBLIC antlr4-runtime)
include(extern/diskann/diskann.cmake)
target_compile_definitions(diskann PRIVATE BOOST_NO_CXX98_FUNCTION_BASE)
target_link_libraries(diskann fmt::fmt)
set(DIST_CONTAINS_NEON ON)
set(DIST_CONTAINS_SVE OFF)
set(VSAG_VERSION "129b82c-ios")
configure_file(src/version.h.in src/version.h @ONLY)
include_directories("${CMAKE_BINARY_DIR}/src")
add_subdirectory(src)
target_include_directories(vsag_static BEFORE PRIVATE "${CMAKE_BINARY_DIR}/src")
target_link_libraries(vsag_static ${BLAS_LIBRARIES})
