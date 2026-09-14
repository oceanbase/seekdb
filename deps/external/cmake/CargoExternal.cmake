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

function(_seekdb_external_cargo_metadata output_variable)
  get_property(_metadata GLOBAL PROPERTY SEEKDB_EXTERNAL_CARGO_METADATA)
  if(NOT _metadata)
    find_program(SEEKDB_EXTERNAL_CARGO cargo
      HINTS "$ENV{CARGO_HOME}/bin" "$ENV{HOME}/.cargo/bin" REQUIRED)
    set(_manifest "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../Cargo.toml")
    set(_lockfile "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../Cargo.lock")
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
      "${_manifest}" "${_lockfile}" "${_toolchain_manifest}")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E env "RUSTUP_TOOLCHAIN=${_toolchain}"
        "${SEEKDB_EXTERNAL_CARGO}" metadata --locked --format-version 1
        --manifest-path "${_manifest}"
      WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
      OUTPUT_VARIABLE _metadata
      ERROR_VARIABLE _error
      RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
      message(FATAL_ERROR "Cargo external source download failed: ${_error}")
    endif()
    set_property(GLOBAL PROPERTY SEEKDB_EXTERNAL_CARGO_METADATA "${_metadata}")
  endif()
  set(${output_variable} "${_metadata}" PARENT_SCOPE)
endfunction()

function(seekdb_external_cargo_package_dir output_variable package_name)
  _seekdb_external_cargo_metadata(_metadata)
  string(JSON _package_count LENGTH "${_metadata}" packages)
  math(EXPR _last_package "${_package_count} - 1")
  unset(_package_dir)
  foreach(_index RANGE 0 ${_last_package})
    string(JSON _name GET "${_metadata}" packages ${_index} name)
    if(_name STREQUAL "${package_name}")
      string(JSON _manifest GET "${_metadata}" packages ${_index} manifest_path)
      string(JSON _source GET "${_metadata}" packages ${_index} source)
      if(NOT _source MATCHES "^registry\\+")
        message(FATAL_ERROR "Expected registry package ${package_name}")
      endif()
      get_filename_component(_package_dir "${_manifest}" DIRECTORY)
      break()
    endif()
  endforeach()
  if(NOT _package_dir)
    message(FATAL_ERROR "Cargo external package not found: ${package_name}")
  endif()
  set(${output_variable} "${_package_dir}" PARENT_SCOPE)
endfunction()

function(seekdb_external_c_compiler_command output_variable)
  set(_compiler "${CMAKE_C_COMPILER}")
  if(CMAKE_C_COMPILER_LAUNCHER)
    list(JOIN CMAKE_C_COMPILER_LAUNCHER " " _launcher)
    set(_compiler "${_launcher} ${CMAKE_C_COMPILER}")
  endif()
  set(${output_variable} "${_compiler}" PARENT_SCOPE)
endfunction()
