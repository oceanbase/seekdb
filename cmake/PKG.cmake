# macOS command-line installer package.
set(CPACK_GENERATOR "productbuild")
set(CPACK_COMPONENTS_IGNORE_GROUPS 0)

set(CPACK_PRODUCTBUILD_IDENTIFIER "com.seekdb.server")
set(CPACK_PRODUCTBUILD_DOMAINS TRUE)
set(CPACK_PRODUCTBUILD_DOMAINS_ANYWHERE FALSE)
set(CPACK_PRODUCTBUILD_DOMAINS_USER FALSE)
set(CPACK_PRODUCTBUILD_DOMAINS_ROOT TRUE)

set(MACOS_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
execute_process(
  COMMAND sw_vers -productVersion
  OUTPUT_VARIABLE MACOS_VERSION_FULL
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REPLACE "." ";" MACOS_VERSION_LIST "${MACOS_VERSION_FULL}")
list(GET MACOS_VERSION_LIST 0 MACOS_VERSION_MAJOR)
set(CPACK_SYSTEM_NAME "macos${MACOS_VERSION_MAJOR}")

include(cmake/Pack.cmake)

set(CPACK_PACKAGE_RELEASE ${OB_RELEASEID})
set(CPACK_PACKAGE_FILE_NAME
  "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_PACKAGE_RELEASE}-${CPACK_SYSTEM_NAME}-${MACOS_ARCH}")

set(CPACK_PREFLIGHT_SERVER_SCRIPT
  "${CMAKE_CURRENT_SOURCE_DIR}/tools/macpkg/launchd/profile/preinstall")
set(CPACK_POSTFLIGHT_SERVER_SCRIPT
  "${CMAKE_CURRENT_SOURCE_DIR}/tools/macpkg/launchd/profile/postinstall")

if (BUILD_CDC_ONLY)
  message(STATUS "seekdb build cdc only")
  set(CPACK_COMPONENTS_ALL cdc)
  set(CPACK_PACKAGE_NAME "seekdb-cdc")
else()
  add_custom_target(bitcode_to_elf ALL
    DEPENDS ${BITCODE_TO_ELF_LIST})
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tools/ocp/software_package.template
              ${CMAKE_CURRENT_SOURCE_DIR}/tools/ocp/software_package
              @ONLY)

install(FILES
  tools/ocp/software_package
  DESTINATION opt/homebrew/share/seekdb/software_package
  COMPONENT server)

message(STATUS "Cpack Components:${CPACK_COMPONENTS_ALL}")
set(CPACK_CMAKE_GENERATOR "Ninja")

include(CPack)

add_custom_target(pkg
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target package
  )

# Re-run CPack without depending on the full build. Use this after changing
# packaging scripts or install rules when binaries in build_macpkg are current.
add_custom_target(pkg-only
  COMMAND ${CMAKE_CPACK_COMMAND} --config ${CMAKE_BINARY_DIR}/CPackConfig.cmake
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  )

set(CPACK_RESOURCE_FILE_LICENSE
  "${CMAKE_CURRENT_SOURCE_DIR}/tools/macpkg/LICENSE.txt")
# 可选
# set(CPACK_RESOURCE_FILE_WELCOME "${CMAKE_CURRENT_SOURCE_DIR}/tools/macpkg/WELCOME.txt")
# set(CPACK_RESOURCE_FILE_README  "${CMAKE_CURRENT_SOURCE_DIR}/tools/macpkg/README.txt")
