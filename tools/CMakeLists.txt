if (BUILD_CDC_ONLY)
  add_subdirectory(ob_error EXCLUDE_FROM_ALL)
  add_subdirectory(ob_admin EXCLUDE_FROM_ALL)
elseif(WIN32)
  # ob_error uses <getopt.h> / getopt_long which are POSIX-only, and needs
  # _ALLOW_COMPILER_AND_STL_VERSION_MISMATCH to compile against the current
  # MSVC STL with clang-cl 18. Exclude from default build on Windows.
  add_subdirectory(ob_error EXCLUDE_FROM_ALL)
  add_subdirectory(ob_admin EXCLUDE_FROM_ALL)
else()
  add_subdirectory(ob_error)
  add_subdirectory(ob_admin)
endif()
add_subdirectory(upgrade)
