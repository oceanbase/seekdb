if(APPLE)
  find_package(GTest REQUIRED)
  set(GTEST_INCLUDE_DIRS "/opt/homebrew/include")
  include_directories(${DEP_DIR}/include)
  include_directories(${GTEST_INCLUDE_DIRS})
endif()

add_library(oblib_testbase INTERFACE)
target_include_directories(oblib_testbase INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(oblib_testbase INTERFACE -lgmock -lgtest)
file(COPY run_tests.sh DESTINATION .)
enable_testing()

add_definitions(-DOB_UNITTEST)

function(oblib_addtest mainfile)
  get_filename_component(testname ${mainfile} NAME_WE)
  add_executable(${testname} ${mainfile})
  add_test(${testname} ${testname})
  if(NOT APPLE)
    target_link_libraries(${testname} PRIVATE -Wl,--whole-archive mock_di -Wl,--no-whole-archive oblib oblib_testbase -static-libgcc -static-libstdc++ ${ARGN}
        ${OB_RELRO_FLAG} -Wl,-T,${CMAKE_SOURCE_DIR}/rpm/ld.lds)
  else()
    target_link_libraries(${testname} PRIVATE mock_di oblib oblib_testbase ${ARGN} ${OB_RELRO_FLAG})
  endif()
endfunction()

function(oblib_addtest_simd mainfile)
  get_filename_component(testname ${mainfile} NAME_WE)
  add_executable(${testname} ${ARGV})
  add_test(${testname} ${testname})
  if(NOT APPLE)
    target_link_libraries(${testname} PRIVATE objit oblib oblib_testbase -static-libgcc -static-libstdc++
        -Wl,-znorelro -Wl,-T,${CMAKE_SOURCE_DIR}/rpm/ld.lds)
  else()
    target_link_libraries(${testname} PRIVATE objit oblib oblib_testbase)
  endif()
  if (${ARCHITECTURE} STREQUAL "x86_64")
	  target_compile_options(${testname}
	    PRIVATE
      -mbmi2 
	  )
  endif()
endfunction()

add_subdirectory(lib)
add_subdirectory(rpc)
add_subdirectory(common)
