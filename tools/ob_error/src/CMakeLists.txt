add_definitions(-D__ERROR_CODE_PARSER_)
if (WIN32)
  # MSVC STL (headers shipped with VS 2022) requires Clang >= 19, but the
  # project's clang-cl is 18. Allow the mismatch.
  add_definitions(-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH)
endif()
set(SRC_LIST ob_error.cpp os_errno.cpp ${CMAKE_SOURCE_DIR}/src/share/ob_errno.cpp)

add_executable(ob_error ${SRC_LIST})

add_library(oberror SHARED ${SRC_LIST})

include_directories(
    ${CMAKE_SOURCE_DIR}/
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/deps/oblib/src
)
