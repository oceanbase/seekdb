# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0

function(seekdb_generate_inner_tables repo output)
  set(SEEKDB_INNER_TABLE_GENERATOR
    "${repo}/src/share/inner_table/generate_inner_table_schema.py")
  set(SEEKDB_INNER_TABLE_DEF
    "${repo}/src/share/inner_table/ob_inner_table_schema_def.py")
  set(SEEKDB_INNER_TABLE_INIT_DATA
    "${repo}/src/share/inner_table/ob_inner_table_init_data.py")
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
  set(SEEKDB_INNER_TABLE_STAMP "${output}/.inner_table_schema.sha256")
  set(SEEKDB_INNER_TABLE_CURRENT_DIGEST "")
  if(EXISTS "${SEEKDB_INNER_TABLE_STAMP}")
    file(READ "${SEEKDB_INNER_TABLE_STAMP}" SEEKDB_INNER_TABLE_CURRENT_DIGEST)
    string(STRIP "${SEEKDB_INNER_TABLE_CURRENT_DIGEST}"
      SEEKDB_INNER_TABLE_CURRENT_DIGEST)
  endif()
  if(NOT "${SEEKDB_INNER_TABLE_CURRENT_DIGEST}" STREQUAL "${SEEKDB_INNER_TABLE_DIGEST}" OR
     NOT EXISTS "${output}/share/inner_table/ob_inner_table_schema.h" OR
     NOT EXISTS "${output}/observer/virtual_table/ob_all_virtual_sqlite_tables.cpp")
    execute_process(
      COMMAND "${Python3_EXECUTABLE}" "${SEEKDB_INNER_TABLE_GENERATOR}"
        --def-file "${SEEKDB_INNER_TABLE_DEF}"
        --share-output-dir "${output}/share/inner_table"
        --observer-output-dir "${output}/observer/virtual_table"
        --quiet
      RESULT_VARIABLE SEEKDB_INNER_TABLE_GENERATOR_RESULT)
    if(NOT SEEKDB_INNER_TABLE_GENERATOR_RESULT EQUAL 0)
      message(FATAL_ERROR "Failed to generate inner-table schema sources")
    endif()
    file(WRITE "${SEEKDB_INNER_TABLE_STAMP}" "${SEEKDB_INNER_TABLE_DIGEST}\n")
  endif()
endfunction()
