sql_unittest(test_parser_perf)
sql_unittest(test_fast_parser)
sql_unittest(test_pl_parser)
sql_unittest(test_parser)
sql_unittest(test_multi_parser)
sql_unittest(test_fts_parser)

# test_sql_fast_parser links only the lightweight proxy parser static lib
# (not full oblib), so the CPU-dispatch global oceanbase::common::arches
# referenced by ob_parse_simd.cpp is otherwise undefined at runtime. Compile
# the defining TU directly into the test to resolve it.
add_executable(test_sql_fast_parser
  test_sql_fast_parser.cpp
  ${CMAKE_SOURCE_DIR}/deps/oblib/src/lib/utility/ob_target_specific.cpp)
target_link_libraries(test_sql_fast_parser
  PRIVATE
  ob_sql_proxy_parser_static
  $<$<PLATFORM_ID:Darwin>:GTest::gtest>
  $<$<NOT:$<PLATFORM_ID:Darwin>>:gtest>
  $<$<NOT:$<PLATFORM_ID:Darwin>>:gmock>
  $<$<NOT:$<PLATFORM_ID:Darwin>>:-static-libstdc++>
)

if(NOT APPLE)
  target_link_options(test_sql_fast_parser PRIVATE LINKER:--unresolved-symbols=ignore-all)
endif()
target_include_directories(test_sql_fast_parser
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/ob_unittest
)
