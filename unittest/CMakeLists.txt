# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0.

find_package(Threads REQUIRED)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/SeekdbModuleTests.cmake")

set(SEEKDB_PRETEST_MODULES oblib share storage sql observer PARENT_SCOPE)
set(SEEKDB_PRETEST_OBLIB_TARGET oblib_tests PARENT_SCOPE)
set(SEEKDB_PRETEST_SHARE_TARGET share_tests PARENT_SCOPE)
set(SEEKDB_PRETEST_STORAGE_TARGET storage_tests PARENT_SCOPE)
set(SEEKDB_PRETEST_SQL_TARGET sql_tests PARENT_SCOPE)
set(SEEKDB_PRETEST_OBSERVER_TARGET observer_tests PARENT_SCOPE)

add_subdirectory(oblib)
add_subdirectory(share)
add_subdirectory(storage)
add_subdirectory(sql)
add_subdirectory(observer)

add_custom_target(pretest DEPENDS
  oblib_tests share_tests storage_tests sql_tests observer_tests)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/run_tests.sh.in"
  "${CMAKE_CURRENT_BINARY_DIR}/run_tests.sh"
  @ONLY)
file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/run_tests.sh"
  PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
              WORLD_READ WORLD_EXECUTE)
