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

# Build a single Rust host archive containing sql-nio and optional plugin
# runtime. Never link independent Rust staticlibs into the same host.
# sql_nio remains a compatible C++ target name for network consumers.
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
# Cargo's aggregate staticlib artifact name is platform-specific.
if(WIN32)
  set(RUST_STATICLIB "${RUST_TARGET_DIR}/${_cargo_out_subdir}/seekdb_host.lib")
else()
  set(RUST_STATICLIB "${RUST_TARGET_DIR}/${_cargo_out_subdir}/libseekdb_host.a")
endif()

# Sources whose change should retrigger a rebuild of the staticlib.
file(GLOB_RECURSE _rust_sources CONFIGURE_DEPENDS
  "${RUST_CRATE_DIR}/src/*.rs"
  "${RUST_WORKSPACE_DIR}/plugin-runtime/src/*.rs"
  "${RUST_WORKSPACE_DIR}/seekdb-host/src/*.rs")
list(APPEND _rust_sources
  "${RUST_WORKSPACE_DIR}/Cargo.toml"
  "${RUST_WORKSPACE_DIR}/rust-toolchain.toml"
  "${RUST_CRATE_DIR}/Cargo.toml"
  "${RUST_WORKSPACE_DIR}/plugin-runtime/Cargo.toml"
  "${RUST_WORKSPACE_DIR}/seekdb-host/Cargo.toml")

set(_rust_host_features)
if(SEEKDB_ENABLE_EXPERIMENTAL_PLUGINS)
  list(APPEND _rust_host_features --features plugins)
endif()
# Make generators must also rerun Cargo when only the feature option changes.
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/seekdb-rust-host-features.txt"
  CONTENT "${_rust_host_features}\n")
list(APPEND _rust_sources "${CMAKE_CURRENT_BINARY_DIR}/seekdb-rust-host-features.txt")

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
          --package seekdb-host ${_rust_host_features}
  WORKING_DIRECTORY "${RUST_WORKSPACE_DIR}"
  DEPENDS ${_rust_sources}
  COMMENT "[rust] cargo build seekdb-host (${_cargo_out_subdir})"
  ${_rust_job_server_options}
  VERBATIM)

add_custom_target(seekdb_rust_host_build DEPENDS "${RUST_STATICLIB}")
add_custom_target(sql_nio_build DEPENDS seekdb_rust_host_build)

# System libraries the Rust std staticlib depends on.
if(WIN32)
  # Win32 libs Rust std's staticlib needs. windows-sys uses raw #[link] that does
  # not emit a /DEFAULTLIB directive for all of these, so list them explicitly --
  # notably ntdll for std's anonymous-pipe path (NtCreateNamedPipeFile).
  set(_rust_syslibs ntdll userenv ws2_32 bcrypt advapi32 synchronization)
else()
  find_package(Threads REQUIRED)
  set(_rust_syslibs Threads::Threads ${CMAKE_DL_LIBS} m)
  if(NOT APPLE)
    list(APPEND _rust_syslibs rt)
  endif()
endif()

add_library(seekdb_rust_host INTERFACE)
add_dependencies(seekdb_rust_host seekdb_rust_host_build)
target_include_directories(seekdb_rust_host INTERFACE
  "${RUST_WORKSPACE_DIR}/plugin-runtime/include")
target_link_libraries(seekdb_rust_host INTERFACE "${RUST_STATICLIB}" ${_rust_syslibs})

add_library(sql_nio INTERFACE)
target_include_directories(sql_nio INTERFACE "${RUST_INCLUDE_DIR}")
target_link_libraries(sql_nio INTERFACE seekdb_rust_host)

set_property(DIRECTORY APPEND PROPERTY
  ADDITIONAL_CLEAN_FILES "${RUST_TARGET_DIR}")

message(STATUS "[rust] seekdb Rust host ready -> ${RUST_STATICLIB}")
