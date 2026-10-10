cmake_minimum_required(VERSION 3.20)
project(seekdb_lapacke LANGUAGES C)

# Build only the LAPACKE entry points used by the pinned VSAG source.
# Accelerate supplies the underlying BLAS and LAPACK implementation.
if(NOT APPLE OR NOT LAPACK_SOURCE)
  message(FATAL_ERROR "An Apple SDK and LAPACK_SOURCE are required")
endif()
set(_lapacke "${LAPACK_SOURCE}/LAPACKE")
set(_sources "${_lapacke}/src/lapacke_nancheck.c")
foreach(_routine sgeqrf sorgqr sgetrf ssyev sgesdd)
  list(APPEND _sources "${_lapacke}/src/lapacke_${_routine}.c"
    "${_lapacke}/src/lapacke_${_routine}_work.c")
endforeach()
file(GLOB _utilities CONFIGURE_DEPENDS "${_lapacke}/utils/*.c")
configure_file("${_lapacke}/include/lapacke_mangling_with_flags.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/include/lapacke_mangling.h" COPYONLY)
add_library(lapacke STATIC ${_sources} ${_utilities})
target_include_directories(lapacke PUBLIC "${_lapacke}/include"
  "${CMAKE_CURRENT_BINARY_DIR}/include")
target_compile_definitions(lapacke PRIVATE LAPACK_F2C)
target_link_libraries(lapacke PUBLIC "-framework Accelerate")
install(TARGETS lapacke ARCHIVE DESTINATION lib)
install(DIRECTORY "${_lapacke}/include/" DESTINATION include
  FILES_MATCHING PATTERN "*.h")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/lapacke_mangling.h"
  DESTINATION include)
