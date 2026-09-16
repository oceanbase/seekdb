# Copyright (c) 2026 OceanBase.
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

include_guard(GLOBAL)

function(seekdb_external_cargo_toolchain cargo_variable toolchain_variable)
  get_property(_cargo GLOBAL PROPERTY SEEKDB_EXTERNAL_CARGO_EXECUTABLE)
  get_property(_toolchain GLOBAL PROPERTY SEEKDB_EXTERNAL_RUST_TOOLCHAIN)
  if(NOT _cargo OR NOT _toolchain)
    find_program(_cargo cargo
      HINTS "$ENV{CARGO_HOME}/bin" "$ENV{HOME}/.cargo/bin" REQUIRED)
    set(_toolchain_manifest "${CMAKE_SOURCE_DIR}/rust/rust-toolchain.toml")
    file(STRINGS "${_toolchain_manifest}" _toolchain_line
      REGEX "^[ \t]*channel[ \t]*=[ \t]*\"[0-9]+\\.[0-9]+\\.[0-9]+\"")
    string(REGEX REPLACE ".*\"([0-9]+\\.[0-9]+\\.[0-9]+)\".*" "\\1"
      _toolchain "${_toolchain_line}")
    if(NOT _toolchain)
      message(FATAL_ERROR
        "Cannot read pinned Rust toolchain from ${_toolchain_manifest}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
      "${_toolchain_manifest}")
    set_property(GLOBAL PROPERTY SEEKDB_EXTERNAL_CARGO_EXECUTABLE "${_cargo}")
    set_property(GLOBAL PROPERTY SEEKDB_EXTERNAL_RUST_TOOLCHAIN "${_toolchain}")
  endif()
  set(${cargo_variable} "${_cargo}" PARENT_SCOPE)
  set(${toolchain_variable} "${_toolchain}" PARENT_SCOPE)
endfunction()

function(seekdb_external_c_compiler_command output_variable)
  set(_compiler "${CMAKE_C_COMPILER}")
  if(CMAKE_C_COMPILER_LAUNCHER)
    list(JOIN CMAKE_C_COMPILER_LAUNCHER " " _launcher)
    set(_compiler "${_launcher} ${CMAKE_C_COMPILER}")
  endif()
  set(${output_variable} "${_compiler}" PARENT_SCOPE)
endfunction()

function(seekdb_external_add_cargo_artifacts)
  set(_one_value_args NAME MANIFEST OUTPUT_ROOT COMMENT)
  set(_multi_value_args OUTPUTS ENV DEPENDS)
  cmake_parse_arguments(ARG "" "${_one_value_args}" "${_multi_value_args}" ${ARGN})
  if(NOT ARG_NAME OR NOT ARG_MANIFEST OR NOT ARG_OUTPUT_ROOT OR NOT ARG_OUTPUTS)
    message(FATAL_ERROR
      "seekdb_external_add_cargo_artifacts requires NAME, MANIFEST, OUTPUT_ROOT and OUTPUTS")
  endif()

  seekdb_external_cargo_toolchain(_cargo _rust_toolchain)
  set(_cargo_target "${ARG_OUTPUT_ROOT}/cargo-target")
  set(_lockfile "${ARG_MANIFEST}")
  cmake_path(REPLACE_FILENAME _lockfile "Cargo.lock")
  set(_toolchain_manifest "${CMAKE_SOURCE_DIR}/rust/rust-toolchain.toml")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${ARG_MANIFEST}" "${_lockfile}")

  add_custom_command(
    OUTPUT ${ARG_OUTPUTS}
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_cargo_target}"
    COMMAND "${CMAKE_COMMAND}" -E env
      "RUSTUP_TOOLCHAIN=${_rust_toolchain}"
      "CARGO_TARGET_DIR=${_cargo_target}"
      "MAKEFLAGS="
      ${ARG_ENV}
      "${_cargo}" build --locked --release --jobs 4
        --manifest-path "${ARG_MANIFEST}"
    DEPENDS
      "${ARG_MANIFEST}"
      "${_lockfile}"
      "${_toolchain_manifest}"
      ${ARG_DEPENDS}
    COMMENT "${ARG_COMMENT}"
    VERBATIM)
  add_custom_target(${ARG_NAME}_build DEPENDS ${ARG_OUTPUTS})
endfunction()
