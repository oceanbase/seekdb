# Unit tests for the file SQL feature. Nothing here is part of the production
# binary: the directory is only added when unittest/CMakeLists.txt exists.
#
# The parent CMakeLists installs the pretest binary by target name, so the
# `sql_tests` target always has to exist. When GoogleTest is unavailable the
# target is created empty and simply runs no tests, which keeps a toolchain
# without the test dependency building successfully.

find_package(GTest QUIET)

# The bundled toolchain ships libgtest.a without a CMake package config, so fall
# back to a plain library lookup before giving up on the real tests.
if(NOT GTest_FOUND AND DEFINED GTEST_ROOT)
  find_library(SEEKDB_GTEST_LIB gtest PATHS "${GTEST_ROOT}/lib" NO_DEFAULT_PATH)
  find_library(SEEKDB_GTEST_MAIN_LIB gtest_main PATHS "${GTEST_ROOT}/lib" NO_DEFAULT_PATH)
  find_path(SEEKDB_GTEST_INCLUDE gtest/gtest.h PATHS "${GTEST_ROOT}/include" NO_DEFAULT_PATH)
  if(SEEKDB_GTEST_LIB AND SEEKDB_GTEST_MAIN_LIB AND SEEKDB_GTEST_INCLUDE)
    set(GTest_FOUND TRUE)
  endif()
endif()

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
if(TARGET GTest::gtest)
  target_link_libraries(sql_tests PRIVATE
    ob_sql_static ob_sql_server_parser_static ob_base GTest::gtest GTest::gtest_main)
else()
  target_link_libraries(sql_tests PRIVATE
    ob_sql_static ob_sql_server_parser_static ob_base
    "${SEEKDB_GTEST_LIB}" "${SEEKDB_GTEST_MAIN_LIB}")
  target_include_directories(sql_tests PRIVATE "${SEEKDB_GTEST_INCLUDE}")
endif()

# lib/lock/ob_lock.h throws ThreadLockedException unless exceptions are disabled,
# which is how the production objects are compiled.
target_compile_definitions(sql_tests PRIVATE _NO_EXCEPTION)

# The same include roots the production objects use; a test that includes the
# parser or the file scanners needs the generated inner-table headers and the
# bundled third-party headers as well.
target_include_directories(sql_tests PRIVATE
  "${CMAKE_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/src/query/api"
  "${CMAKE_SOURCE_DIR}/src/objit/include"
  "${CMAKE_SOURCE_DIR}/src/oblib"
  "${CMAKE_SOURCE_DIR}/src/oblib/common"
  "${CMAKE_SOURCE_DIR}/src/oblib/easy"
  "${CMAKE_SOURCE_DIR}/src/oblib/easy/include"
  "${CMAKE_BINARY_DIR}/generated"
  "${CMAKE_BINARY_DIR}/generated/share"
  "${CMAKE_BINARY_DIR}/generated/share/inner_table")

add_test(NAME sql_tests COMMAND sql_tests)
