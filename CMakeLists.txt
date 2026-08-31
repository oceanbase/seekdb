cmake_minimum_required(VERSION 3.20)

# CMake is the compatibility production/pretest builder while Bazel remains
# the authoritative modular build graph. Keep this surface deliberately
# narrow: one Release-compatible configuration and Unity compilation only.
include(cmake/Utils.cmake)
include(cmake/Env.cmake)

project("OceanBase"
  VERSION 1.4.0.0
  DESCRIPTION "OceanBase seekdb database system"
  HOMEPAGE_URL "https://www.oceanbase.ai"
  LANGUAGES CXX C ASM)

option(OB_ENABLE_STANDBY "Build physical standby support" ON)
option(ENABLE_SANITY "Build with seekdb memory Sanity instrumentation" OFF)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(SEEKDB_GENERATED_ROOT "${CMAKE_BINARY_DIR}/generated")
set(SEEKDB_INNER_TABLE_GENERATOR
  "${CMAKE_SOURCE_DIR}/src/share/inner_table/generate_inner_table_schema.py")
set(SEEKDB_INNER_TABLE_DEF
  "${CMAKE_SOURCE_DIR}/src/share/inner_table/ob_inner_table_schema_def.py")
set(SEEKDB_INNER_TABLE_INIT_DATA
  "${CMAKE_SOURCE_DIR}/src/share/inner_table/ob_inner_table_init_data.py")
set(SEEKDB_INNER_TABLE_INPUTS
  "${SEEKDB_INNER_TABLE_GENERATOR}"
  "${SEEKDB_INNER_TABLE_DEF}"
  "${SEEKDB_INNER_TABLE_INIT_DATA}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${SEEKDB_INNER_TABLE_INPUTS})

set(SEEKDB_INNER_TABLE_INPUT_DIGEST "")
foreach(SEEKDB_INNER_TABLE_INPUT IN LISTS SEEKDB_INNER_TABLE_INPUTS)
  file(SHA256 "${SEEKDB_INNER_TABLE_INPUT}" SEEKDB_INNER_TABLE_INPUT_SHA256)
  string(APPEND SEEKDB_INNER_TABLE_INPUT_DIGEST
    "${SEEKDB_INNER_TABLE_INPUT}:${SEEKDB_INNER_TABLE_INPUT_SHA256}\n")
endforeach()
string(SHA256 SEEKDB_INNER_TABLE_DIGEST "${SEEKDB_INNER_TABLE_INPUT_DIGEST}")
set(SEEKDB_INNER_TABLE_STAMP "${SEEKDB_GENERATED_ROOT}/.inner_table_schema.sha256")
set(SEEKDB_INNER_TABLE_CURRENT_DIGEST "")
if(EXISTS "${SEEKDB_INNER_TABLE_STAMP}")
  file(READ "${SEEKDB_INNER_TABLE_STAMP}" SEEKDB_INNER_TABLE_CURRENT_DIGEST)
  string(STRIP "${SEEKDB_INNER_TABLE_CURRENT_DIGEST}"
    SEEKDB_INNER_TABLE_CURRENT_DIGEST)
endif()
if(NOT "${SEEKDB_INNER_TABLE_CURRENT_DIGEST}" STREQUAL "${SEEKDB_INNER_TABLE_DIGEST}" OR
   NOT EXISTS "${SEEKDB_GENERATED_ROOT}/share/inner_table/ob_inner_table_schema.h" OR
   NOT EXISTS "${SEEKDB_GENERATED_ROOT}/observer/virtual_table/ob_all_virtual_sqlite_tables.cpp")
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${SEEKDB_INNER_TABLE_GENERATOR}"
      --def-file "${SEEKDB_INNER_TABLE_DEF}"
      --share-output-dir "${SEEKDB_GENERATED_ROOT}/share/inner_table"
      --observer-output-dir "${SEEKDB_GENERATED_ROOT}/observer/virtual_table"
      --quiet
    RESULT_VARIABLE SEEKDB_INNER_TABLE_GENERATOR_RESULT)
  if(NOT SEEKDB_INNER_TABLE_GENERATOR_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to generate inner-table schema sources")
  endif()
  file(WRITE "${SEEKDB_INNER_TABLE_STAMP}" "${SEEKDB_INNER_TABLE_DIGEST}\n")
endif()

set(SEEKDB_CMAKE_INVENTORY "${CMAKE_BINARY_DIR}/seekdb_bazel_source_inventory.cmake")
set(SEEKDB_CMAKE_INVENTORY_INPUTS
  "${CMAKE_SOURCE_DIR}/src/oblib/oblib_source_inventory.bzl"
  "${CMAKE_SOURCE_DIR}/src/data_plane/data_plane_build_defs.bzl"
  "${CMAKE_SOURCE_DIR}/src/query/query_build_defs.bzl"
  "${CMAKE_SOURCE_DIR}/src/share/share_source_inventory.bzl"
  "${CMAKE_SOURCE_DIR}/src/sql/sql_source_inventory.bzl"
  "${CMAKE_SOURCE_DIR}/src/storage/storage_source_inventory.bzl"
  "${CMAKE_SOURCE_DIR}/src/storage/storage_build_defs.bzl"
  "${CMAKE_SOURCE_DIR}/src/pl/pl_source_inventory.bzl"
  "${CMAKE_SOURCE_DIR}/src/rootserver/rootserver_source_inventory.bzl"
  "${CMAKE_SOURCE_DIR}/src/logservice/logservice_build_defs.bzl"
  "${CMAKE_SOURCE_DIR}/src/observer/observer_source_inventory.bzl")
foreach(SEEKDB_PRETEST_MODULE
    oblib share storage sql rootserver observer logservice pl query data_plane)
  set(SEEKDB_PRETEST_BUILD_FILE
    "${CMAKE_SOURCE_DIR}/unittest/${SEEKDB_PRETEST_MODULE}/BUILD.bazel")
  if(EXISTS "${SEEKDB_PRETEST_BUILD_FILE}")
    list(APPEND SEEKDB_CMAKE_INVENTORY_INPUTS
      "${SEEKDB_PRETEST_BUILD_FILE}")
  endif()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/tools/cmake/emit_bazel_source_inventory.py"
  ${SEEKDB_CMAKE_INVENTORY_INPUTS})
execute_process(
  COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tools/cmake/emit_bazel_source_inventory.py"
    --repo "${CMAKE_SOURCE_DIR}"
    --output "${SEEKDB_CMAKE_INVENTORY}"
  RESULT_VARIABLE SEEKDB_CMAKE_INVENTORY_RESULT)
if(NOT SEEKDB_CMAKE_INVENTORY_RESULT EQUAL 0)
  message(FATAL_ERROR "Failed to translate Bazel production source inventories")
endif()
include("${SEEKDB_CMAKE_INVENTORY}")

if(NOT CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
  message(FATAL_ERROR
    "The compatibility CMake build supports only release "
    "(-DCMAKE_BUILD_TYPE=RelWithDebInfo)")
endif()
if(NOT OB_ENABLE_UNITY)
  message(FATAL_ERROR "The compatibility CMake build supports only Unity compilation")
endif()
set(SEEKDB_CMAKE_PACKAGE_BUILD OFF)
set(SEEKDB_CMAKE_PACKAGE_FORMAT "")
set(SEEKDB_CMAKE_PACKAGE_FORMAT_COUNT 0)
foreach(SEEKDB_CMAKE_FORMAT RPM DEB TGZ WIX)
  if(CMAKE_BUILD_${SEEKDB_CMAKE_FORMAT})
    message(DEPRECATION
      "CMAKE_BUILD_${SEEKDB_CMAKE_FORMAT} is deprecated; "
      "use OB_BUILD_${SEEKDB_CMAKE_FORMAT} instead")
    set(OB_BUILD_${SEEKDB_CMAKE_FORMAT} ON)
  endif()
endforeach()
foreach(SEEKDB_CMAKE_FORMAT RPM DEB TGZ WIX)
  if(OB_BUILD_${SEEKDB_CMAKE_FORMAT})
    math(EXPR SEEKDB_CMAKE_PACKAGE_FORMAT_COUNT
      "${SEEKDB_CMAKE_PACKAGE_FORMAT_COUNT} + 1")
    set(SEEKDB_CMAKE_PACKAGE_FORMAT "${SEEKDB_CMAKE_FORMAT}")
  endif()
endforeach()
if(SEEKDB_CMAKE_PACKAGE_FORMAT_COUNT GREATER 1)
  message(FATAL_ERROR "Select only one package format: RPM, DEB, TGZ, or WIX")
elseif(SEEKDB_CMAKE_PACKAGE_FORMAT_COUNT EQUAL 1)
  if(NOT OB_BUILD_PACKAGE)
    message(FATAL_ERROR
      "OB_BUILD_${SEEKDB_CMAKE_PACKAGE_FORMAT} requires OB_BUILD_PACKAGE=ON")
  endif()
  if((SEEKDB_CMAKE_PACKAGE_FORMAT STREQUAL "RPM" OR
      SEEKDB_CMAKE_PACKAGE_FORMAT STREQUAL "DEB") AND
     NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
      "The CMake ${SEEKDB_CMAKE_PACKAGE_FORMAT} compatibility build supports Linux only")
  endif()
  if(SEEKDB_CMAKE_PACKAGE_FORMAT STREQUAL "WIX" AND NOT WIN32)
    message(FATAL_ERROR "The CMake WIX compatibility build supports Windows only")
  endif()
  if(ANDROID)
    message(FATAL_ERROR "CMake packaging is not supported for Android")
  endif()
  set(SEEKDB_CMAKE_PACKAGE_BUILD ON)
endif()

if(OB_ERRSIM OR WITH_COVERAGE OR OB_BUILD_CCLS)
  message(FATAL_ERROR
    "The compatibility CMake build supports only Release Unity profiles")
endif()
if(ENABLE_SANITY AND SEEKDB_CMAKE_PACKAGE_BUILD)
  message(FATAL_ERROR "Sanity instrumentation cannot be combined with packaging")
endif()
if((ENABLE_AUTO_FDO OR ENABLE_THIN_LTO OR ENABLE_HOTFUNC OR
    OB_STATIC_LINK_LGPL_DEPS) AND NOT SEEKDB_CMAKE_PACKAGE_BUILD)
  message(FATAL_ERROR
    "Package optimization options require a CMake packaging profile")
endif()
if(OB_BUILD_PACKAGE AND NOT SEEKDB_CMAKE_PACKAGE_BUILD)
  message(FATAL_ERROR
    "OB_BUILD_PACKAGE requires one package format: RPM, DEB, TGZ, or WIX")
endif()

string(TOLOWER "${ARCHITECTURE}" SEEKDB_HOST_ARCHITECTURE)
if(WIN32)
  if(NOT SEEKDB_HOST_ARCHITECTURE MATCHES "^(amd64|x86_64)$")
    message(FATAL_ERROR "The CMake compatibility build supports Windows x64 only")
  endif()
elseif(ANDROID)
  if(NOT SEEKDB_HOST_ARCHITECTURE MATCHES "^(aarch64|arm64)$")
    message(FATAL_ERROR "The CMake compatibility build supports Android arm64-v8a only")
  endif()
elseif(APPLE)
  if(NOT SEEKDB_HOST_ARCHITECTURE MATCHES "^(x86_64|aarch64|arm64)$")
    message(FATAL_ERROR "The CMake compatibility build supports macOS x86_64/arm64 only")
  endif()
elseif(UNIX)
  if(NOT SEEKDB_HOST_ARCHITECTURE MATCHES "^(x86_64|aarch64|arm64)$")
    message(FATAL_ERROR "The CMake compatibility build supports Linux x86_64/aarch64 only")
  endif()
else()
  message(FATAL_ERROR "Unsupported CMake compatibility platform")
endif()

if(WIN32)
  find_program(LLVM_LIB_PROGRAM llvm-lib)
  if(LLVM_LIB_PROGRAM)
    set(CMAKE_AR "${LLVM_LIB_PROGRAM}" CACHE FILEPATH "Archiver" FORCE)
  endif()
  add_compile_definitions(strncasecmp=_strnicmp strcasecmp=_stricmp)
endif()

if(ANDROID)
  add_definitions(-D_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION)
  add_compile_options(-include ${CMAKE_SOURCE_DIR}/include/android_compat.h)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(STDC_SHARED_LIB "")
set(STDC_SHARED_LIB_SYMLINK "")

if(APPLE)
  set(CMAKE_NO_SYSTEM_FROM_IMPORTED 1)
  if(POLICY CMP0152)
    cmake_policy(SET CMP0152 NEW)
  endif()
  set(STDC_SHARED_LIB_SYMLINK "${DEVTOOLS_DIR}/lib64/libstdc++.dylib")
elseif(UNIX)
  set(STDC_SHARED_LIB_SYMLINK "${DEVTOOLS_DIR}/lib64/libstdc++.so")
endif()

if((CMAKE_SYSTEM_NAME STREQUAL "Linux" OR APPLE) AND NOT OB_USE_ASAN)
  set(JEMALLOC_STATIC_LIBRARY "${DEP_DIR}/lib/libjemalloc_pic.a")
  if(NOT EXISTS "${JEMALLOC_STATIC_LIBRARY}")
    message(FATAL_ERROR "Packaged jemalloc not found: ${JEMALLOC_STATIC_LIBRARY}")
  endif()

  set(OB_HAVE_BUNDLED_JEMALLOC TRUE)
endif()

set(SEEKDB_SANITY_NO_BUILTIN_COMPILE_OPTIONS "")
set(SEEKDB_SANITY_WRAP_LINK_OPTIONS "")
if(ENABLE_SANITY)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Sanity instrumentation is supported only on Linux")
  elseif(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Sanity instrumentation requires Clang")
  elseif(NOT OB_HAVE_BUNDLED_JEMALLOC)
    message(FATAL_ERROR "Sanity instrumentation requires bundled jemalloc")
  endif()

  set(SEEKDB_SANITY_PASS "${DEVTOOLS_DIR}/lib64/libsanitypass.so")
  set(SEEKDB_SANITY_HEADER "${DEVTOOLS_DIR}/include/sanity/sanity.h")
  foreach(SEEKDB_SANITY_REQUIRED_FILE
      "${SEEKDB_SANITY_PASS}"
      "${SEEKDB_SANITY_HEADER}")
    if(NOT EXISTS "${SEEKDB_SANITY_REQUIRED_FILE}")
      message(FATAL_ERROR
        "Required Sanity component not found: ${SEEKDB_SANITY_REQUIRED_FILE}")
    endif()
  endforeach()

  add_compile_definitions(ENABLE_SANITY)
  set(SEEKDB_SANITY_NO_BUILTIN_COMPILE_OPTIONS
    -fno-builtin-memset
    -fno-builtin-bzero
    -fno-builtin-memcpy
    -fno-builtin-memmove
    -fno-builtin-memcmp
    -fno-builtin-strlen
    -fno-builtin-strnlen
    -fno-builtin-strcpy
    -fno-builtin-strncpy
    -fno-builtin-strcmp
    -fno-builtin-strncmp
    -fno-builtin-strcasecmp
    -fno-builtin-strncasecmp
    -fno-builtin-vsprintf
    -fno-builtin-vsnprintf
    -fno-builtin-sprintf
    -fno-builtin-snprintf)
  set(SEEKDB_SANITY_WRAP_LINK_OPTIONS
    -Wl,--wrap=memcpy
    -Wl,--wrap=memmove
    -Wl,--wrap=memset
    -Wl,--wrap=bzero
    -Wl,--wrap=memcmp
    -Wl,--wrap=strlen
    -Wl,--wrap=strnlen
    -Wl,--wrap=strcpy
    -Wl,--wrap=strncpy
    -Wl,--wrap=strcmp
    -Wl,--wrap=strncmp
    -Wl,--wrap=strcasecmp
    -Wl,--wrap=strncasecmp
    -Wl,--wrap=vsprintf
    -Wl,--wrap=vsnprintf
    -Wl,--wrap=sprintf
    -Wl,--wrap=snprintf)
  message(STATUS "Sanity instrumentation enabled: ${SEEKDB_SANITY_PASS}")
endif()

# Only resolve path if it exists (on macOS, this directory may not exist)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-#pragma-messages")
if(EXISTS "${STDC_SHARED_LIB_SYMLINK}")
  file(REAL_PATH "${STDC_SHARED_LIB_SYMLINK}" STDC_SHARED_LIB)
  message(STATUS "STDC_SHARED_LIB: ${STDC_SHARED_LIB}")
endif()

# Rust owns the MySQL protocol/NIO engine.  Keep Cargo's artifact inside the
# selected CMake build directory and expose its C ABI header to the migrated
# src/oblib and Observer sources.
include(cmake/Rust.cmake)
include_directories("${CMAKE_SOURCE_DIR}/rust/sql-nio/include")

add_subdirectory(src)

# Farm compile and pretest still publish and consume liboberror.so.  Keep the
# legacy error-code utility available as an explicit compatibility target
# without adding it to the default production build.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_subdirectory(tools/ob_error EXCLUDE_FROM_ALL)
endif()

# The farm pretest contract remains available during the CMake transition.
# Other unit-test workflows and their architecture rules stay Bazel-owned.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux"
   AND EXISTS "${CMAKE_SOURCE_DIR}/unittest/CMakeLists.txt")
  enable_testing()
  add_subdirectory(unittest EXCLUDE_FROM_ALL)
  foreach(SEEKDB_PRETEST_MODULE IN LISTS SEEKDB_PRETEST_MODULES)
    string(TOUPPER "${SEEKDB_PRETEST_MODULE}" SEEKDB_PRETEST_MODULE_UPPER)
    set(SEEKDB_PRETEST_PREFIX
      "SEEKDB_PRETEST_${SEEKDB_PRETEST_MODULE_UPPER}")
    set(SEEKDB_PRETEST_TARGET "${${SEEKDB_PRETEST_PREFIX}_TARGET}")
    install(PROGRAMS "$<TARGET_FILE:${SEEKDB_PRETEST_TARGET}>"
      DESTINATION "unittest/${SEEKDB_PRETEST_MODULE}"
      COMPONENT pretest)
  endforeach()
endif()

if(SEEKDB_CMAKE_PACKAGE_FORMAT STREQUAL "RPM")
  include(cmake/RPM.cmake)
elseif(SEEKDB_CMAKE_PACKAGE_FORMAT STREQUAL "DEB")
  include(cmake/DEB.cmake)
elseif(SEEKDB_CMAKE_PACKAGE_FORMAT STREQUAL "TGZ")
  include(cmake/TGZ.cmake)
elseif(SEEKDB_CMAKE_PACKAGE_FORMAT STREQUAL "WIX")
  include(cmake/WIX.cmake)
endif()
