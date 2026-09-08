# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
function(seekdb_generate_syspack repo output release result)
  set(source "${repo}/src/share/inner_table/sys_package")
  file(GLOB sql_files CONFIGURE_DEPENDS "${source}/*.sql")
  set(generated "${output}/syspack_source.cpp")
  add_custom_command(
    OUTPUT "${generated}" "${release}/.stamp"
    COMMAND "${CMAKE_COMMAND}" -E remove_directory "${output}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${output}" "${release}"
    COMMAND "${CMAKE_COMMAND}" -E copy ${sql_files} "${source}/syspack_codegen.py" "${output}"
    COMMAND "${Python3_EXECUTABLE}" "${output}/syspack_codegen.py" -rd "${release}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${release}/.stamp"
    DEPENDS ${sql_files} "${source}/syspack_codegen.py"
    WORKING_DIRECTORY "${repo}"
    COMMENT "Generating embedded system package sources"
    VERBATIM)
  set_source_files_properties("${generated}" PROPERTIES GENERATED TRUE)
  set(${result} "${generated}" PARENT_SCOPE)
endfunction()
