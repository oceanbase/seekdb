define_property(GLOBAL PROPERTY ANDROID_TEST_SOURCES
  BRIEF_DOCS "Sources for combined Android all_tests binary"
  FULL_DOCS "Accumulated by ob_unittest and friends")

# Register a test source for the combined Android binary
function(_android_register_test_source src)
  if(ANDROID)
    set_property(GLOBAL APPEND PROPERTY ANDROID_TEST_SOURCES "${src}")
  endif()
endfunction()

function(ob_unittest case)
  if(ARGC EQUAL 1)
    add_executable(${case} ${case}.cpp)
    _android_register_test_source("${CMAKE_CURRENT_SOURCE_DIR}/${case}.cpp")
  else()
    add_executable(${ARGV})
    # First arg is target name, rest are sources
    list(GET ARGV 0 _target)
    list(REMOVE_AT ARGV 0)
    foreach(_src ${ARGV})
      get_filename_component(_abs "${_src}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
      _android_register_test_source("${_abs}")
    endforeach()
  endif()
  if (case MATCHES "^test_.*")
    add_test(${case} ${case})
    set_tests_properties(${case} PROPERTIES TIMEOUT 300)
  endif()
  if(ANDROID)
    target_link_libraries(${case} PRIVATE -Wl,--whole-archive mock_di -Wl,--no-whole-archive oceanbase_static gtest gmock)
  elseif(NOT APPLE)
    target_link_libraries(${case} PRIVATE -Wl,--whole-archive mock_di -Wl,--no-whole-archive oceanbase gtest gmock)
  else()
    target_link_libraries(${case} PRIVATE mock_di oceanbase gtest gmock)
  endif()
  target_include_directories(${case}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/unittest ${CMAKE_SOURCE_DIR}/mittest ${CMAKE_SOURCE_DIR}/deps/oblib/unittest)
  # oblib_grpc generates grpc/*.grpc.pb.h at build time; many headers (e.g. via ob_ls.h ->
  # ob_restore_helper_ctx.h -> ob_storage_grpc.h) need those files before any TU compiles.
  # add_dependencies(ob_storage oblib_grpc) is not enough: unittest objects can compile in parallel.
  add_dependencies(${case} oblib_grpc)
endfunction()

file(COPY . DESTINATION . FILES_MATCHING PATTERN "*.test")
file(COPY . DESTINATION . FILES_MATCHING PATTERN "*.sql")
file(COPY . DESTINATION . FILES_MATCHING PATTERN "*.result")
file(COPY . DESTINATION . FILES_MATCHING PATTERN "*.schema")
file(COPY . DESTINATION . FILES_MATCHING PATTERN "*.cfg")
file(COPY . DESTINATION . FILES_MATCHING PATTERN "*.sh")
file(COPY run_tests.sh DESTINATION .)

if(APPLE)
  find_package(GTest REQUIRED)
  set(GTEST_INCLUDE_DIRS "/opt/homebrew/include")
  include_directories(${DEP_DIR}/include)
  include_directories(${GTEST_INCLUDE_DIRS})
endif()

add_subdirectory(sql)
# add_subdirectory(libobcdc)
add_subdirectory(backup)
# add_subdirectory(archive)
add_subdirectory(logservice)
add_subdirectory(storage)
add_subdirectory(observer)
add_subdirectory(share)
add_subdirectory(lib)
add_subdirectory(rootserver)
add_subdirectory(tools)
# add_subdirectory(data_dictionary)
# add_subdirectory(logminer)
add_subdirectory(plugin)

# Android: single combined test binary (all TEST() macros in one executable)
# Uses sources registered by ob_unittest() and friends via ANDROID_TEST_SOURCES property.
if(ANDROID)
  get_property(ANDROID_ALL_TEST_SOURCES GLOBAL PROPERTY ANDROID_TEST_SOURCES)
  # Exclude tests that need libraries not in oceanbase_static
  list(FILTER ANDROID_ALL_TEST_SOURCES EXCLUDE REGEX "/tools/")
  list(FILTER ANDROID_ALL_TEST_SOURCES EXCLUDE REGEX "/tx/it/")
  list(LENGTH ANDROID_ALL_TEST_SOURCES _n)
  message(STATUS "all_tests: ${_n} test sources collected")

  add_executable(all_tests EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/all_tests_main.cpp
    ${ANDROID_ALL_TEST_SOURCES})
  # Extra sources required by specific test macros (rs_unittest, aggr_unittest2)
  target_sources(all_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/rootserver/ob_rs_test_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/rootserver/fake_freeze_info_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/rootserver/fake_zone_merge_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sql/engine/test_op_engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sql/engine/ob_fake_table_scan_vec_op.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/storage/ob_truncate_info_helper.cpp)
  # Rename each file's main() so only all_tests_main.cpp provides the real one
  target_compile_definitions(all_tests PRIVATE main=disabled_main)
  target_link_options(all_tests PRIVATE -Wl,--allow-multiple-definition)
  target_link_libraries(all_tests PRIVATE
    -Wl,--whole-archive mock_di -Wl,--no-whole-archive
    oceanbase_static gtest gmock
    mockcontainer sql_ut_base mock_tx_log_adapter
    mock_ls_tablet_service mock_access_service)
  target_include_directories(all_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/unittest
    ${CMAKE_SOURCE_DIR}/mittest
    ${CMAKE_SOURCE_DIR}/deps/oblib/unittest)
  # Add test subdirectories so relative includes (e.g. ../share/...) resolve
  set(_android_test_inc_dirs backup lib logservice observer plugin rootserver share sql storage)
  foreach(_dir ${_android_test_inc_dirs})
    target_include_directories(all_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/${_dir})
  endforeach()
endif()
