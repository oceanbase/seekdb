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

# Build independent Rust static libraries and expose explicit C++ link targets.
#
# Usage from any C++ target:
#     target_link_libraries(<your_target> PRIVATE sql_nio)
#     #include "nio.h"
#
# Override the workspace location by setting RUST_WORKSPACE_DIR before include().

if(NOT DEFINED RUST_WORKSPACE_DIR)
  set(RUST_WORKSPACE_DIR "${CMAKE_SOURCE_DIR}/rust")
endif()

# Locate cargo: explicit -DCARGO=, else PATH, else the rustup default location.
if(NOT CARGO)
  find_program(CARGO cargo HINTS "$ENV{CARGO_HOME}/bin" "$ENV{HOME}/.cargo/bin")
endif()
if(NOT CARGO)
  message(FATAL_ERROR "[rust] cargo not found. Install via https://rustup.rs, "
                      "or pass -DCARGO=/path/to/cargo.")
endif()
message(STATUS "[rust] cargo: ${CARGO}")

# Map the CMake build type to a cargo profile and its output subdirectory.
# Debug uses the dedicated cmake-debug profile (dev codegen + panic="abort")
# rather than plain dev, so no linked build can unwind a panic into C++.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(_cargo_profile_flag "--profile" "cmake-debug")
  set(_cargo_out_subdir "cmake-debug")
else()
  set(_cargo_profile_flag "--release")
  set(_cargo_out_subdir "release")
endif()

# Cargo otherwise builds for the macOS host during an Android CMake cross-build.
# Keep the Rust static library on the same target and API level as the C++ code.
set(_cargo_target_args)
set(_cargo_target_subdir)
if(ANDROID)
  if(NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "[rust] unsupported Android ABI: ${CMAKE_ANDROID_ARCH_ABI}")
  endif()
  set(_rust_target_triple "aarch64-linux-android")
  list(APPEND _cargo_target_args "--target" "${_rust_target_triple}")
  set(_cargo_target_subdir "${_rust_target_triple}/")
endif()

# Keep all cargo output inside the CMake build tree (isolated per build dir).
set(RUST_TARGET_DIR "${CMAKE_BINARY_DIR}/rust-target")
# CC/AR: cargo inherits CMake's PATH but not its compiler variables, and
# `ring` (rustls's crypto backend) compiles C through the `cc` crate. Pin it
# to the same toolchain as the rest of the build instead of whatever `cc`
# discovers on PATH.
set(_rust_build_env "CARGO_TARGET_DIR=${RUST_TARGET_DIR}"
                    "CC=${CMAKE_C_COMPILER}" "AR=${CMAKE_AR}")
if(ANDROID)
  string(REGEX REPLACE "^android-" "" _android_api "${ANDROID_PLATFORM}")
  get_filename_component(_ndk_toolchain_bin "${CMAKE_C_COMPILER}" DIRECTORY)
  set(_android_clang "${_ndk_toolchain_bin}/aarch64-linux-android${_android_api}-clang")
  set(_android_ar "${_ndk_toolchain_bin}/llvm-ar")
  if(NOT EXISTS "${_android_clang}")
    message(FATAL_ERROR "[rust] Android clang wrapper not found: ${_android_clang}")
  endif()
  list(APPEND _rust_build_env
       "CC_aarch64_linux_android=${_android_clang}"
       "AR_aarch64_linux_android=${_android_ar}"
       "CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER=${_android_clang}")
endif()
if(WIN32)
  # rustup treats an exact-version override and the stable alias as distinct
  # installed toolchains, even when stable currently is that exact version.
  # Reuse the installed alias only after proving that it satisfies our pin;
  # this avoids a needless network sync without weakening reproducibility.
  file(STRINGS "${RUST_WORKSPACE_DIR}/rust-toolchain.toml" _rust_channel_line
       REGEX "^[ \t]*channel[ \t]*=[ \t]*\"[0-9]+\\.[0-9]+\\.[0-9]+\"")
  string(REGEX REPLACE ".*\"([0-9]+\\.[0-9]+\\.[0-9]+)\".*" "\\1"
         _rust_pinned_version "${_rust_channel_line}")
  find_program(RUSTUP rustup HINTS "$ENV{CARGO_HOME}/bin" "$ENV{USERPROFILE}/.cargo/bin")
  if(RUSTUP AND _rust_pinned_version)
    execute_process(
      COMMAND "${RUSTUP}" run stable rustc --version
      OUTPUT_VARIABLE _stable_rustc_version
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
    if(_stable_rustc_version MATCHES "^rustc ${_rust_pinned_version} ")
      list(APPEND _rust_build_env "RUSTUP_TOOLCHAIN=stable")
      message(STATUS "[rust] reusing installed stable alias for pinned rustc ${_rust_pinned_version}")
    endif()
  endif()
endif()
if(APPLE)
  if(OB_MACOS27)
    list(APPEND _rust_build_env "DEVELOPER_DIR=${OB_MACOS_DEVELOPER_DIR}")
  endif()
  # CMake injects -isysroot into its own compile rules on Apple; the cc crate
  # gets no such implicit flag, so the vendored devtools clang cannot find the
  # macOS SDK headers (TargetConditionals.h). SDKROOT is the env var the clang
  # driver itself honors.
  if(CMAKE_OSX_SYSROOT)
    list(APPEND _rust_build_env "SDKROOT=${CMAKE_OSX_SYSROOT}")
  else()
    execute_process(COMMAND xcrun --show-sdk-path
                    OUTPUT_VARIABLE _macos_sdk_path
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)
    if(_macos_sdk_path)
      list(APPEND _rust_build_env "SDKROOT=${_macos_sdk_path}")
    endif()
  endif()
endif()

set(_rust_job_server_options)
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.28")
  # Preserve GNU Make's jobserver file descriptors for Cargo. Without this,
  # Cargo sees --jobserver-auth in MAKEFLAGS but cannot use the closed FDs.
  list(APPEND _rust_job_server_options JOB_SERVER_AWARE TRUE)
endif()

# System libraries the Rust std staticlib depends on.
if(WIN32)
  # Win32 libs Rust std's staticlib needs. windows-sys uses raw #[link] that does
  # not emit a /DEFAULTLIB directive for all of these, so list them explicitly --
  # notably ntdll for std's anonymous-pipe path (NtCreateNamedPipeFile).
  set(_rust_syslibs ntdll userenv ws2_32 bcrypt advapi32 synchronization)
else()
  find_package(Threads REQUIRED)
  set(_rust_syslibs Threads::Threads ${CMAKE_DL_LIBS} m)
  if(NOT APPLE AND NOT ANDROID)
    list(APPEND _rust_syslibs rt)
  endif()
endif()

# All Rust C ABI libraries share the sql-nio toolchain, header-generation and
# linking setup. Each package still produces its own explicitly linked archive.
function(add_rust_ffi_library target package header)
  set(crate_dir "${RUST_WORKSPACE_DIR}/${package}")
  if(WIN32)
    set(archive "${RUST_TARGET_DIR}/${_cargo_out_subdir}/${target}.lib")
  else()
    set(archive "${RUST_TARGET_DIR}/${_cargo_target_subdir}${_cargo_out_subdir}/lib${target}.a")
  endif()
  file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${crate_dir}/src/*.rs")
  list(APPEND sources
    "${RUST_WORKSPACE_DIR}/Cargo.toml"
    "${RUST_WORKSPACE_DIR}/rust-toolchain.toml"
    "${RUST_WORKSPACE_DIR}/build-support/ffi.rs"
    "${crate_dir}/Cargo.toml"
    "${crate_dir}/build.rs"
    "${crate_dir}/cbindgen.toml")
  add_custom_command(
    OUTPUT "${archive}"
    BYPRODUCTS "${crate_dir}/include/${header}"
    COMMAND "${CMAKE_COMMAND}" -E env ${_rust_build_env}
            "${CARGO}" build ${_cargo_profile_flag} ${_cargo_target_args}
            --manifest-path "${RUST_WORKSPACE_DIR}/Cargo.toml"
            --package "${package}"
    WORKING_DIRECTORY "${RUST_WORKSPACE_DIR}"
    DEPENDS ${sources}
    COMMENT "[rust] cargo build ${package} (${_cargo_out_subdir})"
    ${_rust_job_server_options}
    VERBATIM)
  add_custom_target(${target}_build DEPENDS "${archive}")
  add_library(${target} INTERFACE)
  add_dependencies(${target} ${target}_build)
  target_include_directories(${target} INTERFACE "${crate_dir}/include")
  target_link_libraries(${target} INTERFACE "${archive}" ${_rust_syslibs})
  message(STATUS "[rust] ${target} target ready -> ${archive}")
endfunction()

add_rust_ffi_library(sql_nio sql-nio nio.h)
add_rust_ffi_library(embedding_response embedding-response embedding.h)

set_property(DIRECTORY APPEND PROPERTY
  ADDITIONAL_CLEAN_FILES "${RUST_TARGET_DIR}")
