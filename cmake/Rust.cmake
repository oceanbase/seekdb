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

# Build the Rust crate `sql-nio` (rust/sql-nio) with Cargo and expose it to the
# C++ build as the imported-style INTERFACE target `sql_nio`.
#
# Usage from any C++ target:
#     target_link_libraries(<your_target> PRIVATE sql_nio)
#     #include "nio.h"
#
# Override the workspace location by setting RUST_WORKSPACE_DIR before include().

if(NOT DEFINED RUST_WORKSPACE_DIR)
  set(RUST_WORKSPACE_DIR "${CMAKE_SOURCE_DIR}/rust")
endif()
set(RUST_CRATE_DIR   "${RUST_WORKSPACE_DIR}/sql-nio")
set(RUST_INCLUDE_DIR "${RUST_CRATE_DIR}/include")

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

# Keep all cargo output inside the CMake build tree (isolated per build dir).
set(RUST_TARGET_DIR "${CMAKE_BINARY_DIR}/rust-target")

# CC/AR: cargo inherits CMake's PATH but not its compiler variables, and
# `ring` (rustls's crypto backend) compiles C through the `cc` crate. Pin it
# to the same toolchain as the rest of the build instead of whatever `cc`
# discovers on PATH.
set(_rust_build_env "CARGO_TARGET_DIR=${RUST_TARGET_DIR}"
                    "CC=${CMAKE_C_COMPILER}" "AR=${CMAKE_AR}")
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

# Android NDK: cargo must cross-compile for the Android target triple instead
# of the host. Without --target, cargo builds sql-nio for the host (macOS) and
# ring's cc crate then drives the NDK clang with Apple target flags, which
# fails on Apple-only headers (TargetConditionals.h). Wire the per-target
# CC/AR/linker env at the NDK clang wrappers so ring's C compiles for Android.
set(_rust_cargo_target_args "")
set(_rust_target_subdir "")
if(OB_ANDROID)
  # ABI -> Rust target triple (seekdb's build.sh pins arm64-v8a; map the rest).
  if(ANDROID_ABI STREQUAL "arm64-v8a")
    set(_rust_android_target "aarch64-linux-android")
    set(_rust_android_clang_prefix "aarch64-linux-android")
  elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    # Note the NDK clang wrapper for 32-bit ARM is armv7a-, the ar wrapper armv7-.
    set(_rust_android_target "armv7-linux-androideabi")
    set(_rust_android_clang_prefix "armv7a-linux-androideabi")
  elseif(ANDROID_ABI STREQUAL "x86")
    set(_rust_android_target "i686-linux-android")
    set(_rust_android_clang_prefix "i686-linux-android")
  elseif(ANDROID_ABI STREQUAL "x86_64")
    set(_rust_android_target "x86_64-linux-android")
    set(_rust_android_clang_prefix "x86_64-linux-android")
  else()
    message(FATAL_ERROR "[rust] unsupported ANDROID_ABI '${ANDROID_ABI}'")
  endif()

  # NDK toolchain bin dir: prefer the toolchain's own var, else CMAKE_ANDROID_NDK,
  # else derive the NDK root from CMAKE_TOOLCHAIN_FILE (same walk as src/include/CMakeLists.txt).
  if(ANDROID_TOOLCHAIN_ROOT)
    set(_rust_android_bin "${ANDROID_TOOLCHAIN_ROOT}/bin")
  else()
    set(_rust_ndk "")
    if(CMAKE_ANDROID_NDK)
      set(_rust_ndk "${CMAKE_ANDROID_NDK}")
    elseif(CMAKE_TOOLCHAIN_FILE)
      get_filename_component(_rust_ndk "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
      get_filename_component(_rust_ndk "${_rust_ndk}" DIRECTORY)
      get_filename_component(_rust_ndk "${_rust_ndk}" DIRECTORY)
    endif()
    if(_rust_ndk)
      file(GLOB _rust_ndk_bin_dirs "${_rust_ndk}/toolchains/llvm/prebuilt/*/bin")
      if(_rust_ndk_bin_dirs)
        list(GET _rust_ndk_bin_dirs 0 _rust_android_bin)
      endif()
    endif()
  endif()
  if(NOT _rust_android_bin)
    message(FATAL_ERROR "[rust] cannot locate the NDK toolchain bin dir "
                        "(set ANDROID_TOOLCHAIN_ROOT or configure with the NDK toolchain file)")
  endif()

  # ANDROID_PLATFORM is either android-28 or 28; the NDK clang wrapper is
  # suffixed with the API level (aarch64-linux-android28-clang).
  string(REGEX REPLACE "^android-" "" _rust_android_api "${ANDROID_PLATFORM}")
  if(NOT _rust_android_api MATCHES "^[0-9]+$")
    set(_rust_android_api "28")
  endif()

  # cargo/cc-rs per-target env names: triple uppercased with '-' -> '_'
  # (e.g. aarch64_linux_android).
  string(TOUPPER "${_rust_android_target}" _rust_android_env)
  string(REPLACE "-" "_" _rust_android_env "${_rust_android_env}")

  list(APPEND _rust_build_env
    "CC_${_rust_android_env}=${_rust_android_bin}/${_rust_android_clang_prefix}${_rust_android_api}-clang"
    "AR_${_rust_android_env}=${_rust_android_bin}/llvm-ar"
    "CARGO_TARGET_${_rust_android_env}_LINKER=${_rust_android_bin}/${_rust_android_clang_prefix}${_rust_android_api}-clang++")
  set(_rust_cargo_target_args "--target" "${_rust_android_target}")
  set(_rust_target_subdir "${_rust_android_target}/")
endif()

# Cargo's staticlib artifact name is platform-specific: libsql_nio.a on
# Unix/MSYS, sql_nio.lib with the MSVC toolchain; with --target the artifact
# nests under <target-triple>/.
if(WIN32)
  set(RUST_STATICLIB "${RUST_TARGET_DIR}/${_rust_target_subdir}${_cargo_out_subdir}/sql_nio.lib")
else()
  set(RUST_STATICLIB "${RUST_TARGET_DIR}/${_rust_target_subdir}${_cargo_out_subdir}/libsql_nio.a")
endif()

# Sources whose change should retrigger a rebuild of the staticlib.
file(GLOB_RECURSE _rust_sources CONFIGURE_DEPENDS "${RUST_CRATE_DIR}/src/*.rs")
list(APPEND _rust_sources
  "${RUST_WORKSPACE_DIR}/Cargo.toml"
  "${RUST_WORKSPACE_DIR}/rust-toolchain.toml"
  "${RUST_CRATE_DIR}/Cargo.toml")

set(_rust_job_server_options)
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.28")
  # Preserve GNU Make's jobserver file descriptors for Cargo. Without this,
  # Cargo sees --jobserver-auth in MAKEFLAGS but cannot use the closed FDs.
  list(APPEND _rust_job_server_options JOB_SERVER_AWARE TRUE)
endif()

add_custom_command(
  OUTPUT "${RUST_STATICLIB}"
  COMMAND "${CMAKE_COMMAND}" -E env ${_rust_build_env}
          "${CARGO}" build ${_cargo_profile_flag}
          --manifest-path "${RUST_WORKSPACE_DIR}/Cargo.toml"
          --package sql-nio ${_rust_cargo_target_args}
  WORKING_DIRECTORY "${RUST_WORKSPACE_DIR}"
  DEPENDS ${_rust_sources}
  COMMENT "[rust] cargo build sql-nio (${_cargo_out_subdir})"
  ${_rust_job_server_options}
  VERBATIM)

add_custom_target(sql_nio_build DEPENDS "${RUST_STATICLIB}")

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
    # glibc < 2.17 needed librt for clock_*; Android's bionic merged rt into
    # libc and ships no librt.so, so -lrt would fail the final link.
    list(APPEND _rust_syslibs rt)
  endif()
endif()

add_library(sql_nio INTERFACE)
add_dependencies(sql_nio sql_nio_build)
target_include_directories(sql_nio INTERFACE "${RUST_INCLUDE_DIR}")
target_link_libraries(sql_nio INTERFACE "${RUST_STATICLIB}" ${_rust_syslibs})

set_property(DIRECTORY APPEND PROPERTY
  ADDITIONAL_CLEAN_FILES "${RUST_TARGET_DIR}")

message(STATUS "[rust] sql_nio target ready -> ${RUST_STATICLIB}")
