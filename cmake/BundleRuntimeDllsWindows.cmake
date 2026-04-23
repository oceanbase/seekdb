# ---------------------------------------------------------------------------
# BundleRuntimeDllsWindows.cmake
#
# Invoked at BUILD TIME via `cmake -P` to copy third-party runtime DLLs next
# to the Windows executable so it can run directly from the build tree
# without requiring the user's PATH to contain vcpkg / vsag bin directories.
#
# This mirrors the install-time bundling in cmake/Pack.cmake (which runs
# during cpack/install), but targets the build tree so `build/.../seekdb.exe`
# is usable immediately after a build.
#
# Required -D variables:
#   EXE         : absolute path to the built executable (e.g. seekdb.exe)
#   OUT_DIR     : directory to place bundled DLLs (usually dirname of EXE)
#   SEARCH_DIRS : ';'-separated list of directories to search DLLs in
#                 (typically ${OB_VCPKG_DIR}/bin and ${OB_VSAG_DIR}/bin)
#
# Notes:
# - file(GET_RUNTIME_DEPENDENCIES) requires CMake >= 3.21.
# - System DLLs (kernel32, advapi32, ...) are absent from SEARCH_DIRS and
#   therefore silently skipped; they ship with every Windows installation.
# - copy_if_different keeps incremental builds cheap.
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.21)

if(NOT EXE)
  message(FATAL_ERROR "BundleRuntimeDllsWindows: -DEXE=<path> is required")
endif()
if(NOT OUT_DIR)
  message(FATAL_ERROR "BundleRuntimeDllsWindows: -DOUT_DIR=<dir> is required")
endif()
if(NOT SEARCH_DIRS)
  message(FATAL_ERROR "BundleRuntimeDllsWindows: -DSEARCH_DIRS=<dir;dir> is required")
endif()

file(TO_CMAKE_PATH "${EXE}" EXE)
file(TO_CMAKE_PATH "${OUT_DIR}" OUT_DIR)

set(_dirs "")
foreach(_d IN LISTS SEARCH_DIRS)
  if(_d STREQUAL "")
    continue()
  endif()
  file(TO_CMAKE_PATH "${_d}" _d)
  if(IS_DIRECTORY "${_d}")
    list(APPEND _dirs "${_d}")
  else()
    message(STATUS "BundleRuntimeDllsWindows: skip missing dir '${_d}'")
  endif()
endforeach()

if(NOT _dirs)
  message(WARNING "BundleRuntimeDllsWindows: no valid SEARCH_DIRS; nothing to do")
  return()
endif()

if(NOT EXISTS "${EXE}")
  message(FATAL_ERROR "BundleRuntimeDllsWindows: executable not found: ${EXE}")
endif()

file(MAKE_DIRECTORY "${OUT_DIR}")

file(GET_RUNTIME_DEPENDENCIES
  EXECUTABLES
    "${EXE}"
  RESOLVED_DEPENDENCIES_VAR _resolved
  UNRESOLVED_DEPENDENCIES_VAR _unresolved
  CONFLICTING_DEPENDENCIES_PREFIX _conflicts
  DIRECTORIES
    ${_dirs}
  PRE_EXCLUDE_REGEXES
    "^api-ms-"
    "^ext-ms-"
  POST_EXCLUDE_REGEXES
    "[Ss]ystem32"
    "[Ss]yswow64"
)

set(_bundled 0)

# Helper: copy ${_name} from the first SEARCH_DIR that contains it.
function(_bundle_one _name)
  foreach(_dir IN LISTS _dirs)
    if(EXISTS "${_dir}/${_name}")
      execute_process(
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${_dir}/${_name}" "${OUT_DIR}/${_name}"
        RESULT_VARIABLE _rc)
      if(_rc EQUAL 0)
        math(EXPR _new "${_bundled} + 1")
        set(_bundled ${_new} PARENT_SCOPE)
      else()
        message(WARNING "BundleRuntimeDllsWindows: failed to copy ${_name} from ${_dir}")
      endif()
      return()
    endif()
  endforeach()
endfunction()

foreach(_file IN LISTS _resolved)
  get_filename_component(_name "${_file}" NAME)
  _bundle_one("${_name}")
endforeach()

# Conflicting dependencies: same DLL present in multiple DIRECTORIES (e.g.
# vcpkg and vsag both ship a copy). Prefer the first SEARCH_DIR wins order.
foreach(_name IN LISTS _conflicts_FILENAMES)
  _bundle_one("${_name}")
endforeach()

if(_unresolved)
  # These are DLLs that couldn't be located in SEARCH_DIRS and aren't filtered
  # as system libraries. They usually indicate a missing dependency in
  # SEARCH_DIRS; report but don't fail the build.
  list(REMOVE_DUPLICATES _unresolved)
  foreach(_u IN LISTS _unresolved)
    message(STATUS "BundleRuntimeDllsWindows: unresolved (likely system) ${_u}")
  endforeach()
endif()

message(STATUS "BundleRuntimeDllsWindows: bundled ${_bundled} DLLs -> ${OUT_DIR}")
