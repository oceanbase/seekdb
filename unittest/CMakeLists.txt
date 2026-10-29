# Unit tests for the file SQL feature. Nothing here is part of the production
# binary: the directory is only added when unittest/CMakeLists.txt exists.
#
# The parent CMakeLists installs the pretest binary by target name, so the
# `sql_tests` target always has to exist. When GoogleTest is unavailable the
# target is created empty and simply runs no tests, which keeps a toolchain
# without the test dependency building successfully.

find_package(GTest QUIET)

if(NOT GTest_FOUND)
  message(STATUS "GoogleTest not found; file SQL unit tests are not built")
  add_executable(sql_tests EXCLUDE_FROM_ALL "${CMAKE_CURRENT_SOURCE_DIR}/empty_main.cpp")
  return()
endif()

# Tests are listed explicitly rather than globbed so a renamed file fails the
# build instead of being dropped from the suite silently.
set(SEEKDB_FILE_SQL_TEST_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/sql/engine/basic/test_file_scan_utils.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/sql/parser/test_file_sql_parser.cpp")

foreach(SEEKDB_TEST_SOURCE IN LISTS SEEKDB_FILE_SQL_TEST_SOURCES)
  if(NOT EXISTS "${SEEKDB_TEST_SOURCE}")
    message(FATAL_ERROR "missing test source: ${SEEKDB_TEST_SOURCE}")
  endif()
endforeach()

add_executable(sql_tests ${SEEKDB_FILE_SQL_TEST_SOURCES})
target_link_libraries(sql_tests PRIVATE
  ob_sql_static ob_sql_server_parser_static ob_base GTest::gtest
  GTest::gtest_main)
target_include_directories(sql_tests PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/src/oblib"
  "${CMAKE_BINARY_DIR}/generated")
add_test(NAME sql_tests COMMAND sql_tests)
