if (BUILD_CDC_ONLY)
  add_subdirectory(ob_error EXCLUDE_FROM_ALL)
  add_subdirectory(ob_admin EXCLUDE_FROM_ALL)
elseif(WIN32)
  # ob_error: getopt_long is shimmed inline in ob_error.cpp on Windows, and
  # _ALLOW_COMPILER_AND_STL_VERSION_MISMATCH is set in ob_error/src/CMakeLists.txt
  # to allow clang-cl 18 against the current MSVC STL.
  add_subdirectory(ob_error)
  add_subdirectory(ob_admin EXCLUDE_FROM_ALL)
else()
  add_subdirectory(ob_error)
  add_subdirectory(ob_admin)
endif()
add_subdirectory(upgrade)
