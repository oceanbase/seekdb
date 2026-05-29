if(NOT WIN32)
  return()
endif()

set(_wepoll_src "${CMAKE_CURRENT_SOURCE_DIR}/wepoll.c")
set(_wepoll_hdr "${CMAKE_CURRENT_SOURCE_DIR}/wepoll.h")
if(NOT EXISTS "${_wepoll_src}" OR NOT EXISTS "${_wepoll_hdr}")
  message(FATAL_ERROR
    "wepoll source not found.\n"
    "  Drop wepoll.h and wepoll.c (BSD-2-Clause) from\n"
    "  https://github.com/piscisaureus/wepoll into ${CMAKE_CURRENT_SOURCE_DIR}/\n"
    "  See deps/oblib/src/lib/wepoll/README.md.")
endif()

add_library(wepoll STATIC "${_wepoll_src}")
target_include_directories(wepoll PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# wepoll resolves AFD ioctls at runtime via ntdll
target_link_libraries(wepoll PUBLIC ws2_32 ntdll)
# wepoll.c is third-party; do not let project -Werror flags fail its build
if(MSVC)
  target_compile_options(wepoll PRIVATE /w)
else()
  target_compile_options(wepoll PRIVATE -w)
endif()
