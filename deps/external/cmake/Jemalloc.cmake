# Download the registry source package; build jemalloc outside Cargo's cache.
include(ExternalProject)
find_program(JEMALLOC_MAKE NAMES gmake make REQUIRED)
seekdb_external_cargo_package_dir(_jemalloc_package_dir "seekdb-jemalloc-sys")
set(_jemalloc_source "${_jemalloc_package_dir}/vendor/jemalloc")
if(NOT EXISTS "${_jemalloc_source}/configure")
  message(FATAL_ERROR "Cargo package does not contain jemalloc configure")
endif()
set(_jemalloc_root "${CMAKE_BINARY_DIR}/third-party/jemalloc")
set(JEMALLOC_STATIC_LIBRARY "${_jemalloc_root}/build/lib/libjemalloc_pic.a")
set(JEMALLOC_INCLUDE_DIR "${_jemalloc_root}/build/include")
set(JEMALLOC_PUBLIC_HEADER "${JEMALLOC_INCLUDE_DIR}/jemalloc/jemalloc.h")
set(_jemalloc_cflags "-O2 -fPIC")
if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND GCC9)
  string(APPEND _jemalloc_cflags " --gcc-toolchain=${GCC9}")
endif()
seekdb_external_c_compiler_command(_jemalloc_cc)
set(_jemalloc_platform_env "")
if(APPLE)
  set(_jemalloc_osx_architectures "${CMAKE_OSX_ARCHITECTURES}")
  if(NOT _jemalloc_osx_architectures)
    set(_jemalloc_osx_architectures "${ARCHITECTURE}")
  endif()
  foreach(_jemalloc_osx_architecture IN LISTS _jemalloc_osx_architectures)
    string(APPEND _jemalloc_cflags " -arch ${_jemalloc_osx_architecture}")
  endforeach()
  if(CMAKE_OSX_DEPLOYMENT_TARGET)
    string(APPEND _jemalloc_cflags
      " -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    list(APPEND _jemalloc_platform_env
      "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
  endif()
  if(CMAKE_OSX_SYSROOT)
    list(APPEND _jemalloc_platform_env "SDKROOT=${CMAKE_OSX_SYSROOT}")
  endif()
endif()
set(_jemalloc_env
  "CC=${_jemalloc_cc}"
  "AR=${CMAKE_AR}"
  "CFLAGS=${_jemalloc_cflags}"
  ${_jemalloc_platform_env})
if(CMAKE_CROSSCOMPILING)
  message(FATAL_ERROR "Cargo jemalloc build currently requires a native Linux/macOS toolchain")
endif()
ExternalProject_Add(seekdb_jemalloc_build
  SOURCE_DIR "${_jemalloc_source}"
  BINARY_DIR "${_jemalloc_root}/build"
  PREFIX "${_jemalloc_root}/stamps"
  DOWNLOAD_COMMAND "" UPDATE_COMMAND "" PATCH_COMMAND ""
  CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E env ${_jemalloc_env}
    sh "${_jemalloc_source}/configure" --with-version=VERSION
    "--prefix=${_jemalloc_root}/install" --with-jemalloc-prefix=je_
    --enable-static --disable-shared --disable-cxx --disable-doc --enable-stats
  BUILD_COMMAND "${CMAKE_COMMAND}" -E env "MAKEFLAGS="
    "${JEMALLOC_MAKE}" -j4 build_lib_static
  INSTALL_COMMAND ""
  BUILD_BYPRODUCTS "${JEMALLOC_STATIC_LIBRARY}" "${JEMALLOC_PUBLIC_HEADER}")
add_library(seekdb_jemalloc STATIC IMPORTED GLOBAL)
file(MAKE_DIRECTORY "${JEMALLOC_INCLUDE_DIR}")
set_target_properties(seekdb_jemalloc PROPERTIES
  IMPORTED_LOCATION "${JEMALLOC_STATIC_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${JEMALLOC_INCLUDE_DIR}")
add_dependencies(seekdb_jemalloc seekdb_jemalloc_build)
message(STATUS "jemalloc registry source: ${_jemalloc_source}")
