# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0.

function(seekdb_add_module_tests TARGET_NAME)
  add_executable(${TARGET_NAME} EXCLUDE_FROM_ALL
    "${CMAKE_SOURCE_DIR}/unittest/all_tests_main.cpp"
    ${ARGN})
  target_include_directories(${TARGET_NAME} PRIVATE "${DEP_DIR}/include")
  target_link_libraries(${TARGET_NAME} PRIVATE
    oceanbase
    "${DEP_DIR}/lib/libgtest.a"
    Threads::Threads
    ${CMAKE_DL_LIBS})
  string(REGEX REPLACE "_tests$" "" MODULE_NAME "${TARGET_NAME}")
  set_target_properties(${TARGET_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/unittest/${MODULE_NAME}"
    BUILD_RPATH "${CMAKE_BINARY_DIR}/src/observer")
  add_test(NAME ${TARGET_NAME}_shard_0 COMMAND ${TARGET_NAME})
  set_tests_properties(${TARGET_NAME}_shard_0 PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endfunction()

function(seekdb_add_module_benchmark TARGET_NAME)
  add_executable(${TARGET_NAME} EXCLUDE_FROM_ALL ${ARGN})
  target_include_directories(${TARGET_NAME} PRIVATE
    "${CMAKE_SOURCE_DIR}/src/oblib"
    "${DEP_DIR}/include")
  target_link_libraries(${TARGET_NAME} PRIVATE Threads::Threads)
  set_target_properties(${TARGET_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/unittest/benchmark")
endfunction()
