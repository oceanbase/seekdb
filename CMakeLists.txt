cmake_minimum_required(VERSION 3.20)

include(cmake/Utils.cmake)
include(cmake/Env.cmake)

project("OceanBase"
  VERSION 1.3.0.0
  DESCRIPTION "OceanBase seekdb database system"
  HOMEPAGE_URL "https://www.oceanbase.ai"
  LANGUAGES CXX C ASM)

# Android NDK toolchain / defaults may pin C++17 after project(); enforce C++20 for language features (e.g. consteval).
if(ANDROID)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS ON)
endif()

if(WIN32)
  find_program(LLVM_LIB_PROGRAM llvm-lib)
  if(LLVM_LIB_PROGRAM)
    set(CMAKE_AR "${LLVM_LIB_PROGRAM}" CACHE FILEPATH "Archiver" FORCE)
  endif()
  add_compile_definitions(strncasecmp=_strnicmp strcasecmp=_stricmp)
endif()

if(ANDROID)
  # Boost headers use std::unary_function removed in C++17; NDK libc++ supports this compat macro
  add_definitions(-D_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION)
  # Force-include Android compat header for test code portability fixes
  add_compile_options(-include ${CMAKE_SOURCE_DIR}/include/android_compat.h)
endif()

if(OB_ERRSIM)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DERRSIM")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DERRSIM")
endif()

# TODO: remove ENABLE_DEBUG_LOG
if(FALSE AND ENABLE_DEBUG_LOG)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DENABLE_DEBUG_LOG")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DENABLE_DEBUG_LOG")
endif()

if (OB_BUILD_PACKAGE)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DOB_BUILD_PACKAGE")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DOB_BUILD_PACKAGE")
endif()

if (OB_ENABLE_MCMODEL)
  if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mcmodel=large")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcmodel=large")
  endif()
endif()

message(STATUS "This is BINARY dir " ${PROJECT_BINARY_DIR})
message(STATUS "This is SOURCE dir " ${PROJECT_SOURCE_DIR})

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(STDC_SHARED_LIB "")
set(STDC_SHARED_LIB_SYMLINK "")

if(APPLE)
  set(CMAKE_NO_SYSTEM_FROM_IMPORTED 1)
  # Set CMake policy CMP0152 to suppress warning about file(REAL_PATH) behavior
  if(POLICY CMP0152)
    cmake_policy(SET CMP0152 NEW)
  endif()
  set(STDC_SHARED_LIB_SYMLINK "${DEVTOOLS_DIR}/lib64/libstdc++.dylib")
elseif(UNIX)
  set(STDC_SHARED_LIB_SYMLINK "${DEVTOOLS_DIR}/lib64/libstdc++.so")
endif()
# Disable Boost header deprecation warnings (clang uses -Wno-#pragma-messages)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-#pragma-messages")

include(cmake/Jemalloc.cmake)

# Only resolve path if it exists (on macOS, this directory may not exist)
if(EXISTS "${STDC_SHARED_LIB_SYMLINK}")
  file(REAL_PATH ${STDC_SHARED_LIB_SYMLINK} STDC_SHARED_LIB)
  message(STATUS "STDC_SHARED_LIB: ${STDC_SHARED_LIB}")
endif()

# Rust network engine: defines the mandatory `sql_nio` target linked into the
# observer.
include(cmake/Rust.cmake)
# Make the Rust/C++ ABI header visible to oblib.
include_directories(${CMAKE_SOURCE_DIR}/rust/sql-nio/include)

add_subdirectory(deps/oblib)
include(${CMAKE_SOURCE_DIR}/cmake/EnsureWindowsDiaGuids.cmake)
add_subdirectory(src)

include(CMakeDependentOption)
# OB_BUILD_PACKAGE => include tools and build them.
# otherwise    => include tools but don't build them.
option(OB_INCLUDE_TOOLS "" ON)
cmake_dependent_option(
  OB_BUILD_TOOLS "Build tools" ON
  "OB_BUILD_PACKAGE" OFF)
# OB_BUILD_PACKAGE => don't include unittest or build them.
# otherwise    => include unittest but don't build them.
cmake_dependent_option(
  OB_INCLUDE_UNITTEST "Include unittest" ON
  "NOT OB_BUILD_PACKAGE" OFF)
option(OB_BUILD_UNITTEST "" OFF)

# OB_BUILD_PACKAGE => don't include test or build them.
# otherwise    => include test but don't build them.
cmake_dependent_option(
  OB_INCLUDE_TEST "Include test" ON
  "NOT OB_BUILD_PACKAGE" OFF)
option(OB_BUILD_TEST "" OFF)

include(CTest)
if (OB_BUILD_UNITTEST)
  add_subdirectory(unittest)
elseif(OB_INCLUDE_UNITTEST)
  add_subdirectory(unittest EXCLUDE_FROM_ALL)
endif()

if (OB_BUILD_TOOLS)
  add_subdirectory(tools)
elseif (OB_INCLUDE_TOOLS)
  add_subdirectory(tools EXCLUDE_FROM_ALL)
endif()

if(ANDROID)
  # Android: no packaging needed
elseif(APPLE)
  if(CMAKE_BUILD_TGZ)
    include(cmake/TGZ.cmake)
  endif()
elseif(UNIX)
  if (CMAKE_BUILD_RPM)
    include(cmake/RPM.cmake)
  elseif(CMAKE_BUILD_DEB)
    include(cmake/DEB.cmake)
  else()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      include(FindPkgConfig)
      if(PKG_CONFIG_FOUND)
        execute_process(
          COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=systemdsystemunitdir systemd
          RESULT_VARIABLE result_var
          OUTPUT_VARIABLE output_var
          ERROR_QUIET
          OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if("${result_var}" STREQUAL "0" AND "${output_var}" STREQUAL "/lib/systemd/system")
          include(cmake/DEB.cmake)
        else()
          include(cmake/RPM.cmake)
        endif()
      endif()
    else()
      message(FATAL_ERROR "Unsupported system: ${CMAKE_SYSTEM_NAME}")
    endif()
  endif()
elseif(WIN32)
  if(OB_BUILD_PACKAGE)
    message(STATUS "Windows package environment adapting")
    include(cmake/WIX.cmake)
  endif()
endif()

# === oblib module-layer DAG check ===========================================
# Standalone make target (any build): cd <build_dir> && make module_layer_check
# Validates module layering from the include graph (config: cmake/module_check/module_layers.conf):
# a module may only include modules with layer <= its own; an upward edge exits non-zero with location + fix hint.
# (Replaces the old decl-use/modulemap approach: the include graph alone suffices for layer governance.)
add_custom_target(module_layer_check
  COMMAND python3 ${CMAKE_SOURCE_DIR}/cmake/module_check/module_layer_check.py ${CMAKE_SOURCE_DIR}
  COMMENT "Checking module-layer DAG (include graph)"
  VERBATIM)
# Strict gate: fail if any baseline residual remains (milestone/release check for "is oblib fully clean").
# Strict gate: fail if any baseline residual remains (milestone/release check).
add_custom_target(module_layer_check_strict
  COMMAND python3 ${CMAKE_SOURCE_DIR}/cmake/module_check/module_layer_check.py ${CMAKE_SOURCE_DIR} --strict
  COMMENT "Checking module-layer DAG (strict: fail on any residual)"
  VERBATIM)
# Legacy alias (muscle-memory / external scripts): make oblib_layer_check[_strict] still works.
# Legacy alias (muscle-memory / external scripts): make oblib_layer_check[_strict] still works.
add_custom_target(oblib_layer_check)
add_dependencies(oblib_layer_check module_layer_check)
add_custom_target(oblib_layer_check_strict)
add_dependencies(oblib_layer_check_strict module_layer_check_strict)

# Wiring: hang the layer check as a seekdb dependency. It has no deps of its own, so it runs in parallel with
# compilation from the start (2s text scan, zero extra wall-clock); an upward edge fails make seekdb (fail-closed).
add_dependencies(seekdb module_layer_check)
