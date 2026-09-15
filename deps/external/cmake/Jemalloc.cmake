# Build the Cargo-locked jemalloc package and export its native artifacts.
seekdb_external_cargo_toolchain(_jemalloc_cargo _jemalloc_rust_toolchain)
seekdb_external_c_compiler_command(_jemalloc_cc)

set(_jemalloc_manifest "${CMAKE_CURRENT_LIST_DIR}/../Cargo.toml")
set(_jemalloc_lockfile "${CMAKE_CURRENT_LIST_DIR}/../Cargo.lock")
set(_jemalloc_toolchain_manifest "${CMAKE_SOURCE_DIR}/rust/rust-toolchain.toml")
set(_jemalloc_root "${CMAKE_BINARY_DIR}/third-party/jemalloc")
set(_jemalloc_cargo_target "${_jemalloc_root}/cargo-target")
set(JEMALLOC_STATIC_LIBRARY "${_jemalloc_root}/lib/libjemalloc_pic.a")
set(JEMALLOC_INCLUDE_DIR "${_jemalloc_root}/include")
set(JEMALLOC_PUBLIC_HEADER "${JEMALLOC_INCLUDE_DIR}/jemalloc/jemalloc.h")

set(_jemalloc_cflags "-O2 -fPIC")
if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND GCC9)
  string(APPEND _jemalloc_cflags " --gcc-toolchain=${GCC9}")
endif()
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
if(CMAKE_CROSSCOMPILING)
  message(FATAL_ERROR "Cargo jemalloc build currently requires a native Linux/macOS toolchain")
endif()
set(_jemalloc_env
  "RUSTUP_TOOLCHAIN=${_jemalloc_rust_toolchain}"
  "CARGO_TARGET_DIR=${_jemalloc_cargo_target}"
  "MAKEFLAGS="
  "CC=${_jemalloc_cc}"
  "AR=${CMAKE_AR}"
  "CFLAGS=${_jemalloc_cflags}"
  "JEMALLOC_SYS_CONFIGURE_ARGS=--with-jemalloc-prefix=je_"
  "JEMALLOC_SYS_OUTPUT_DIR=${_jemalloc_root}"
  ${_jemalloc_platform_env})

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${_jemalloc_manifest}" "${_jemalloc_lockfile}")
add_custom_command(
  OUTPUT "${JEMALLOC_STATIC_LIBRARY}" "${JEMALLOC_PUBLIC_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_jemalloc_cargo_target}"
  COMMAND "${CMAKE_COMMAND}" -E env ${_jemalloc_env}
    "${_jemalloc_cargo}" build --locked --release --jobs 4
      --manifest-path "${_jemalloc_manifest}"
  DEPENDS
    "${CMAKE_CURRENT_LIST_FILE}"
    "${_jemalloc_manifest}"
    "${_jemalloc_lockfile}"
    "${_jemalloc_toolchain_manifest}"
  COMMENT "Building jemalloc with Cargo"
  VERBATIM)
add_custom_target(seekdb_jemalloc_build
  DEPENDS "${JEMALLOC_STATIC_LIBRARY}" "${JEMALLOC_PUBLIC_HEADER}")

add_library(seekdb_jemalloc STATIC IMPORTED GLOBAL)
file(MAKE_DIRECTORY "${JEMALLOC_INCLUDE_DIR}")
set_target_properties(seekdb_jemalloc PROPERTIES
  IMPORTED_LOCATION "${JEMALLOC_STATIC_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${JEMALLOC_INCLUDE_DIR}")
add_dependencies(seekdb_jemalloc seekdb_jemalloc_build)
