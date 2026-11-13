# Test program for SeekDB C API (links to libseekdb from src/include)
add_executable(test_seekdb
  test_seekdb.cpp
)

target_include_directories(test_seekdb
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/unittest
)

target_link_libraries(test_seekdb
  PRIVATE
    libseekdb
)

# Add test target: first run with default (relative) path
add_test(NAME test_seekdb COMMAND test_seekdb)
set_tests_properties(test_seekdb PROPERTIES TIMEOUT 300)

# Second run with absolute path (same as Rust/Python/Go/Node bindings)
set(SEEKDB_ABS_DIR "${CMAKE_CURRENT_BINARY_DIR}/seekdb_abs.db")
add_test(NAME test_seekdb_absolute COMMAND test_seekdb ${SEEKDB_ABS_DIR})
set_tests_properties(test_seekdb_absolute PROPERTIES TIMEOUT 300 WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

