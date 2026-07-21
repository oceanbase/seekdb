#function(pc_unittest case)
#  sql_unittest(${ARGV})
#  target_sources(${case} PRIVATE test_sql.h test_sql.cpp)
#endfunction()

#pc_unittest(test_id_manager_allocator)
#pc_unittest(test_sql_parameterization)
#pc_unittest(test_pcv_set)
#pc_unittest(test_plan_cache_manager)
#pc_unittest(test_plan_cache_value)
#pc_unittest(test_plan_set)

if(ANDROID)
  ob_unittest(test_session_plan_cache)
else()
  # This test exercises code guarded by ObReqTimeGuard. Link the static server
  # image so the test and plan-cache implementation share one thread-local
  # ObReqTimeInfo instance.
  add_executable(test_session_plan_cache test_session_plan_cache.cpp)
  add_test(test_session_plan_cache test_session_plan_cache)
  set_tests_properties(test_session_plan_cache PROPERTIES TIMEOUT 300)
  if(NOT APPLE)
    target_link_libraries(test_session_plan_cache PRIVATE
      -Wl,--whole-archive mock_di -Wl,--no-whole-archive
      oceanbase_static gtest gmock)
  else()
    target_link_libraries(test_session_plan_cache PRIVATE
      mock_di oceanbase_static gtest gmock)
  endif()
  target_include_directories(test_session_plan_cache PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/unittest
    ${CMAKE_SOURCE_DIR}/mittest
    ${CMAKE_SOURCE_DIR}/deps/oblib/unittest)
endif()
