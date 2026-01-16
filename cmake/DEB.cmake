set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEB_MAIN_COMPONENT "server")
set(CPACK_DEBIAN_SERVER_DEBUGINFO_PACKAGE ON)

include(cmake/Pack.cmake)

# rename server package name
set(CPACK_DEBIAN_SERVER_PACKAGE_NAME ${CPACK_PACKAGE_NAME})
set(CPACK_DEBIAN_PACKAGE_RELEASE ${OB_RELEASEID})

find_program(LSB_RELEASE_EXEC lsb_release)
if(LSB_RELEASE_EXEC)
  execute_process(
    COMMAND ${LSB_RELEASE_EXEC} -is
    OUTPUT_VARIABLE DEBIAN_NAME
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  string(TOLOWER "${DEBIAN_NAME}" DEBIAN_NAME)
  execute_process(
    COMMAND ${LSB_RELEASE_EXEC} -rs
    OUTPUT_VARIABLE DEBIAN_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
endif(LSB_RELEASE_EXEC)
if(DEBIAN_NAME AND DEBIAN_VERSION)
  set(CPACK_DEBIAN_PACKAGE_RELEASE "${CPACK_DEBIAN_PACKAGE_RELEASE}${DEBIAN_NAME}${DEBIAN_VERSION}")
endif()

if (OB_DISABLE_LSE)
  ob_insert_nonlse_to_package_version(${CPACK_DEBIAN_PACKAGE_RELEASE} CPACK_DEBIAN_PACKAGE_RELEASE)
  message(STATUS "CPACK_DEBIAN_PACKAGE_RELEASE: ${CPACK_DEBIAN_PACKAGE_RELEASE}")
endif()

set(CPACK_DEBIAN_PACKAGE_NAME ${CPACK_PACKAGE_NAME})
set(CPACK_PACKAGE_DESCRIPTION ${CPACK_PACKAGE_DESCRIPTION})
set(CPACK_PACKAGE_CONTACT "${OceanBase_HOMEPAGE_URL}")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "OceanBase")
set(CPACK_DEBIAN_PACKAGE_SECTION "database")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "Optional")

# systemd define on deb
set(LIBAIO_DEPENDENCY "libaio1")
if(OB_AIO AND OB_AIO STREQUAL "libaio1t64")
  set(LIBAIO_DEPENDENCY "${OB_AIO}")
endif()
set(CPACK_DEBIAN_SERVER_PACKAGE_DEPENDS "${LIBAIO_DEPENDENCY}, systemd")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tools/systemd/profile/pre_install.sh.template
              ${CMAKE_CURRENT_SOURCE_DIR}/tools/systemd/profile/preinst
              @ONLY)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tools/systemd/profile/post_install.sh.template
              ${CMAKE_CURRENT_SOURCE_DIR}/tools/systemd/profile/postinst
              @ONLY)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tools/systemd/profile/pre_uninstall.sh.template
              ${CMAKE_CURRENT_SOURCE_DIR}/tools/systemd/profile/prerm
              @ONLY)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tools/systemd/profile/post_uninstall.sh.template
              ${CMAKE_CURRENT_SOURCE_DIR}/tools/systemd/profile/postrm
              @ONLY)

set(CPACK_DEBIAN_SERVER_PACKAGE_CONTROL_EXTRA 
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/systemd/profile/postinst 
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/systemd/profile/prerm
  ${CMAKE_CURRENT_SOURCE_DIR}/tools/systemd/profile/postrm)

# add the deb post and pre script
install(FILES
  tools/systemd/profile/postinst
  tools/systemd/profile/prerm
  tools/systemd/profile/postrm
  DESTINATION usr/libexec/seekdb/scripts
  COMPONENT server)

# install cpack to make everything work
include(CPack)

#add deb target to create DEBS
add_custom_target(deb
  COMMAND +make package
  DEPENDS
  observer
  ob_admin ob_error
  ${BITCODE_TO_ELF_LIST}
  )
