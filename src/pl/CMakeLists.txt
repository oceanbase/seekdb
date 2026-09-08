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

set(SYS_PACK_RELEASE_DIR ${CMAKE_BINARY_DIR}/syspack_release CACHE PATH
  "The path to the system package release directory")
include("${PROJECT_SOURCE_DIR}/cmake/generate_syspack.cmake")
seekdb_generate_syspack("${PROJECT_SOURCE_DIR}"
  "${CMAKE_CURRENT_BINARY_DIR}/syspack_codegen" "${SYS_PACK_RELEASE_DIR}" gen_syspack_src)
add_custom_target(generate_syspack_source ALL DEPENDS ${gen_syspack_src})
set_source_files_properties(${gen_syspack_src} PROPERTIES GENERATED TRUE)
ob_set_subtarget(syspack_source ALONE ${gen_syspack_src})
ob_server_add_target(syspack_source)
add_dependencies(syspack_source generate_syspack_source)
