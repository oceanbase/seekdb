# macOS TGZ
set(CPACK_GENERATOR "TGZ")
# not ignore groups, use component packages
set(CPACK_COMPONENTS_IGNORE_GROUPS 0)
set(CPACK_ARCHIVE_THREADS 1)
# Archive generator uses CPACK_ARCHIVE_COMPONENT_INSTALL for component packages.
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)

# Strip binaries to reduce package size
# Note: CPACK_STRIP_FILES doesn't work for Archive generator, use PRE_BUILD_SCRIPTS instead
# Create a script to strip binaries before packaging
# With component install, binaries are in <component>/usr/bin/ subdirectories
file(WRITE ${CMAKE_BINARY_DIR}/cpack_strip.cmake
"message(STATUS \"Stripping binaries in \${CPACK_TEMPORARY_DIRECTORY}...\")
file(GLOB_RECURSE BINARIES \"\${CPACK_TEMPORARY_DIRECTORY}/*/usr/bin/*\")
foreach(BINARY \${BINARIES})
  if(NOT IS_SYMLINK \${BINARY} AND NOT IS_DIRECTORY \${BINARY})
    message(STATUS \"  Stripping \${BINARY}\")
    execute_process(
      COMMAND strip \${BINARY}
      RESULT_VARIABLE STRIP_RESULT
      ERROR_QUIET
    )
  endif()
endforeach()
")
set(CPACK_PRE_BUILD_SCRIPTS ${CMAKE_BINARY_DIR}/cpack_strip.cmake)

set(CPACK_TGZ_FILE_NAME "TGZ-DEFAULT")
set(CMAKE_INSTALL_LIBDIR "lib64")

# get macOS version and architecture information
set(MACOS_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
# get full macOS version like 15.6.1
execute_process(
  COMMAND sw_vers -productVersion
  OUTPUT_VARIABLE MACOS_VERSION_FULL
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

# extract major version like 15.6.1 -> 15
string(REPLACE "." ";" MACOS_VERSION_LIST "${MACOS_VERSION_FULL}")
list(GET MACOS_VERSION_LIST 0 MACOS_VERSION_MAJOR)

# build system name like macos<release> macos15
set(CPACK_SYSTEM_NAME "macos${MACOS_VERSION_MAJOR}")

include(cmake/Pack.cmake)

set(CPACK_PACKAGE_RELEASE ${OB_RELEASEID})
if (BUILD_CDC_ONLY)
  message(STATUS "seekdb build cdc only")
  set(CPACK_COMPONENTS_ALL cdc)
  set(CPACK_PACKAGE_NAME "seekdb-cdc")
else()
  add_custom_target(bitcode_to_elf ALL
    DEPENDS ${BITCODE_TO_ELF_LIST})
endif()

if (OB_BUILD_STANDALONE)
  message(STATUS "seekdb standalone build")
  set(CPACK_PACKAGE_NAME "oceanbase-standalone")
  set(CPACK_COMPONENTS_ALL server libs)
  set(CPACK_ARCHIVE_LIBS_FILE_NAME
    "${CPACK_PACKAGE_NAME}-libs-${CPACK_PACKAGE_VERSION}-${CPACK_PACKAGE_RELEASE}-${CPACK_SYSTEM_NAME}-${MACOS_ARCH}")
endif()

# Per-component archive filenames (no extension).
# - server: drop the "-server" suffix
# - utils: use "seekdb-utils-<version>-<system>"
# Ensure system name is present in archive filenames.
set(CPACK_ARCHIVE_SERVER_FILE_NAME
  "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_PACKAGE_RELEASE}-${CPACK_SYSTEM_NAME}-${MACOS_ARCH}")
set(CPACK_ARCHIVE_UTILS_FILE_NAME
  "${CPACK_PACKAGE_NAME}-utils-${CPACK_PACKAGE_VERSION}-${CPACK_PACKAGE_RELEASE}-${CPACK_SYSTEM_NAME}-${MACOS_ARCH}")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tools/ocp/software_package.template
              ${CMAKE_CURRENT_SOURCE_DIR}/tools/ocp/software_package
              @ONLY)

install(FILES
  tools/ocp/software_package
  DESTINATION usr/share/seekdb/software_package
  COMPONENT server)

message(STATUS "Cpack Components:${CPACK_COMPONENTS_ALL}")

# refs https://stackoverflow.com/questions/48711342/what-does-the-cpack-preinstall-target-do
# see https://cmake.org/cmake/help/latest/module/CPack.html
set(CPACK_CMAKE_GENERATOR "Ninja") # this disables a rebuild i.e. "CPack: - Run preinstall target for..." which seems to be only done for "Unix Makefiles"

# install cpack to make everything work
include(CPack)

# add tgztarget to create TGZ packages
add_custom_target(tgz
  COMMAND +make package
  )
