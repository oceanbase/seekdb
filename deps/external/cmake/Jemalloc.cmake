# Download the registry source package; build jemalloc outside Cargo's cache.
include(ExternalProject)
find_program(CARGO cargo HINTS "$ENV{CARGO_HOME}/bin" "$ENV{HOME}/.cargo/bin" REQUIRED)
find_program(JEMALLOC_MAKE NAMES gmake make REQUIRED)
set(_external_manifest "${CMAKE_CURRENT_LIST_DIR}/../Cargo.toml")
set(_rust_toolchain_manifest "${CMAKE_SOURCE_DIR}/rust/rust-toolchain.toml")
file(STRINGS "${_rust_toolchain_manifest}" _rust_toolchain_line
  REGEX "^[ \t]*channel[ \t]*=[ \t]*\"[0-9]+\\.[0-9]+\\.[0-9]+\"")
string(REGEX REPLACE ".*\"([0-9]+\\.[0-9]+\\.[0-9]+)\".*" "\\1"
  _rust_toolchain "${_rust_toolchain_line}")
if(NOT _rust_toolchain)
  message(FATAL_ERROR "Cannot read pinned Rust toolchain from ${_rust_toolchain_manifest}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${_external_manifest}" "${CMAKE_CURRENT_LIST_DIR}/../Cargo.lock"
  "${_rust_toolchain_manifest}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "RUSTUP_TOOLCHAIN=${_rust_toolchain}"
    "${CARGO}" metadata --locked --format-version 1 --manifest-path "${_external_manifest}"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  OUTPUT_VARIABLE _external_metadata ERROR_VARIABLE _external_error RESULT_VARIABLE _external_result)
if(NOT _external_result EQUAL 0)
  message(FATAL_ERROR "Cargo external source download failed: ${_external_error}")
endif()
string(JSON _package_count LENGTH "${_external_metadata}" packages)
math(EXPR _last_package "${_package_count} - 1")
unset(_jemalloc_source)
foreach(_index RANGE 0 ${_last_package})
  string(JSON _name GET "${_external_metadata}" packages ${_index} name)
  if(_name STREQUAL "seekdb-jemalloc-sys")
    string(JSON _manifest GET "${_external_metadata}" packages ${_index} manifest_path)
    string(JSON _source GET "${_external_metadata}" packages ${_index} source)
    string(JSON _version GET "${_external_metadata}" packages ${_index} version)
    if(NOT _source MATCHES "^registry\\+" OR NOT _version STREQUAL "0.1.0+5.3.1")
      message(FATAL_ERROR "Expected registry seekdb-jemalloc-sys 0.1.0+5.3.1")
    endif()
    get_filename_component(_package_dir "${_manifest}" DIRECTORY)
    set(_jemalloc_source "${_package_dir}/vendor/jemalloc")
  endif()
endforeach()
if(NOT EXISTS "${_jemalloc_source}/configure")
  message(FATAL_ERROR "Cargo package does not contain jemalloc configure")
endif()
set(_jemalloc_root "${CMAKE_BINARY_DIR}/third-party/jemalloc")
set(JEMALLOC_STATIC_LIBRARY "${_jemalloc_root}/build/lib/libjemalloc_pic.a")
set(_jemalloc_cflags "-O2 -fPIC")
if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND GCC9)
  string(APPEND _jemalloc_cflags " --gcc-toolchain=${GCC9}")
endif()
set(_jemalloc_env "CC=${CMAKE_C_COMPILER}" "AR=${CMAKE_AR}" "CFLAGS=${_jemalloc_cflags}")
if(APPLE AND CMAKE_OSX_SYSROOT)
  list(APPEND _jemalloc_env "SDKROOT=${CMAKE_OSX_SYSROOT}")
endif()
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
  BUILD_BYPRODUCTS "${JEMALLOC_STATIC_LIBRARY}")
add_library(seekdb_jemalloc STATIC IMPORTED GLOBAL)
set_target_properties(seekdb_jemalloc PROPERTIES IMPORTED_LOCATION "${JEMALLOC_STATIC_LIBRARY}")
add_dependencies(seekdb_jemalloc seekdb_jemalloc_build)
message(STATUS "jemalloc registry source: ${_jemalloc_source}")
