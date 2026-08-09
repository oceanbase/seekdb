seekdb_apply_unity_inventory(ob_sql SEEKDB_SQL_UNITY)
seekdb_apply_standalone_inventory(ob_sql SEEKDB_SQL_STANDALONE)
seekdb_apply_standalone_inventory(ob_sql_extra SEEKDB_SQL_EXTRA)

ob_add_new_object_target(ob_sql ob_sql)
ob_add_new_object_target(ob_sql_extra ob_sql_extra)
target_link_libraries(ob_sql PUBLIC ob_base)
target_link_libraries(ob_sql_extra PUBLIC ob_base_without_pass)
target_compile_definitions(ob_sql PRIVATE
  SEEKDB_ENABLE_CORE_GIS=$<BOOL:${SEEKDB_ENABLE_CORE_GIS}>)
target_compile_definitions(ob_sql_extra PRIVATE
  SEEKDB_ENABLE_CORE_GIS=$<BOOL:${SEEKDB_ENABLE_CORE_GIS}>)
target_include_directories(ob_sql PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(ob_sql_extra PRIVATE ${PROJECT_SOURCE_DIR}/include)

seekdb_apply_unity_inventory(ob_sql_simd SEEKDB_SQL_SIMD_UNITY)
ob_server_add_target(ob_sql_simd)
if(ARCHITECTURE STREQUAL "x86_64" OR ARCHITECTURE STREQUAL "amd64")
  target_compile_options(ob_sql_simd PRIVATE
    -mtune=core-avx2 -mavx2 -mfma -mbmi2 -mavx512vl -mavx512bw)
endif()

if(WIN32)
  find_program(PWSH_EXE NAMES pwsh pwsh.exe powershell powershell.exe REQUIRED)
  execute_process(
    COMMAND ${PWSH_EXE} -NoProfile -ExecutionPolicy Bypass -File gen_parser_win.ps1
    RESULT_VARIABLE RET
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/src/sql/parser
    OUTPUT_FILE _gen_parser.output
    ERROR_FILE _gen_parser.error)
else()
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E env NEED_PARSER_CACHE=${NEED_PARSER_CACHE} bash gen_parser.sh
    RESULT_VARIABLE RET
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/src/sql/parser
    OUTPUT_FILE _gen_parser.output
    ERROR_FILE _gen_parser.error)
endif()
if(RET AND NOT RET EQUAL 0)
  message(FATAL_ERROR
    "Generate sql parser failed; inspect src/sql/parser/_gen_parser.error")
endif()
add_subdirectory(parser)

add_library(ob_sql_static STATIC EXCLUDE_FROM_ALL)
target_link_libraries(ob_sql_static PUBLIC
  ob_sql ob_sql_extra ob_sql_server_parser_static)
