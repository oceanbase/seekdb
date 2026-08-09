if(WIN32)
  find_program(PWSH_EXE NAMES pwsh pwsh.exe powershell powershell.exe REQUIRED)
  execute_process(
    COMMAND ${PWSH_EXE} -NoProfile -ExecutionPolicy Bypass -File gen_parser_win.ps1
    RESULT_VARIABLE RET
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/src/pl/parser
    OUTPUT_FILE _gen_pl_parser.output
    ERROR_FILE _gen_parser.error)
else()
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E env NEED_PARSER_CACHE=${NEED_PARSER_CACHE} bash gen_parser.sh
    RESULT_VARIABLE RET
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/src/pl/parser
    OUTPUT_FILE _gen_pl_parser.output
    ERROR_FILE _gen_parser.error)
endif()
if(RET AND NOT RET EQUAL 0)
  message(FATAL_ERROR
    "Generate pl parser failed; inspect src/pl/parser/_gen_parser.error")
endif()

seekdb_apply_unity_inventory(ob_pl SEEKDB_PL_UNITY)
seekdb_apply_standalone_inventory(ob_pl SEEKDB_PL_STANDALONE)
ob_server_add_target(ob_pl)

set(SYS_PACK_SQL_SOURCE_DIR ${PROJECT_SOURCE_DIR}/src/share/inner_table/sys_package)
set(SYS_PACK_CODEGEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/syspack_codegen)
set(gen_syspack_src ${SYS_PACK_CODEGEN_DIR}/syspack_source.cpp)
set(SYS_PACK_RELEASE_DIR ${CMAKE_BINARY_DIR}/syspack_release CACHE PATH
  "The path to the system package release directory")
set(SYS_PACK_RELEASE_STAMP ${SYS_PACK_RELEASE_DIR}/.stamp)
file(GLOB SQL_FILES CONFIGURE_DEPENDS "${SYS_PACK_SQL_SOURCE_DIR}/*.sql")

add_custom_command(
  OUTPUT ${gen_syspack_src} ${SYS_PACK_RELEASE_STAMP}
  COMMAND ${CMAKE_COMMAND} -E remove_directory ${SYS_PACK_CODEGEN_DIR}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${SYS_PACK_CODEGEN_DIR}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${SYS_PACK_RELEASE_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy
    ${SQL_FILES} ${SYS_PACK_SQL_SOURCE_DIR}/syspack_codegen.py
    ${SYS_PACK_CODEGEN_DIR}
  COMMAND ${Python3_EXECUTABLE} ${SYS_PACK_CODEGEN_DIR}/syspack_codegen.py
    -rd ${SYS_PACK_RELEASE_DIR}
  COMMAND ${CMAKE_COMMAND} -E touch ${SYS_PACK_RELEASE_STAMP}
  DEPENDS ${SQL_FILES} ${SYS_PACK_SQL_SOURCE_DIR}/syspack_codegen.py
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Generating wrapped syspack source files")
add_custom_target(generate_syspack_source ALL DEPENDS ${gen_syspack_src})
set_source_files_properties(${gen_syspack_src} PROPERTIES GENERATED TRUE)
ob_set_subtarget(syspack_source ALONE ${gen_syspack_src})
ob_server_add_target(syspack_source)
add_dependencies(syspack_source generate_syspack_source)
