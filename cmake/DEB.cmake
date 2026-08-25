set(CPACK_GENERATOR "DEB")
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_IGNORE_GROUPS ON)
set(CPACK_DEB_MAIN_COMPONENT "server")
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")

include(cmake/Pack.cmake)

set(CPACK_DEBIAN_PACKAGE_NAME "${CPACK_PACKAGE_NAME}")
set(CPACK_DEBIAN_SERVER_PACKAGE_NAME "${CPACK_PACKAGE_NAME}")
set(CPACK_DEBIAN_LIBS_PACKAGE_NAME "${CPACK_PACKAGE_NAME}-libs")
set(CPACK_DEBIAN_PACKAGE_VERSION
  "${CPACK_PACKAGE_VERSION}-${SEEKDB_PACKAGE_RELEASE}")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "OceanBase")
set(CPACK_DEBIAN_PACKAGE_SECTION "database")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${OceanBase_HOMEPAGE_URL}")
set(CPACK_DEBIAN_SERVER_PACKAGE_DEPENDS
  "libaio1 | libaio1t64, systemd")

configure_file(
  "${CMAKE_SOURCE_DIR}/tools/systemd/profile/pre_install.sh.template"
  "${SEEKDB_PACKAGE_PROFILE_DIR}/preinst"
  @ONLY)
configure_file(
  "${CMAKE_SOURCE_DIR}/tools/systemd/profile/post_install.sh.template"
  "${SEEKDB_PACKAGE_PROFILE_DIR}/postinst"
  @ONLY)
configure_file(
  "${CMAKE_SOURCE_DIR}/tools/systemd/profile/pre_uninstall.sh.template"
  "${SEEKDB_PACKAGE_PROFILE_DIR}/prerm"
  @ONLY)
configure_file(
  "${CMAKE_SOURCE_DIR}/tools/systemd/profile/post_uninstall.sh.template"
  "${SEEKDB_PACKAGE_PROFILE_DIR}/postrm"
  @ONLY)
file(CHMOD
  "${SEEKDB_PACKAGE_PROFILE_DIR}/preinst"
  "${SEEKDB_PACKAGE_PROFILE_DIR}/postinst"
  "${SEEKDB_PACKAGE_PROFILE_DIR}/prerm"
  "${SEEKDB_PACKAGE_PROFILE_DIR}/postrm"
  PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)

set(CPACK_DEBIAN_SERVER_PACKAGE_CONTROL_EXTRA
  "${SEEKDB_PACKAGE_PROFILE_DIR}/preinst"
  "${SEEKDB_PACKAGE_PROFILE_DIR}/postinst"
  "${SEEKDB_PACKAGE_PROFILE_DIR}/prerm"
  "${SEEKDB_PACKAGE_PROFILE_DIR}/postrm")

install(PROGRAMS
  "${SEEKDB_PACKAGE_PROFILE_DIR}/preinst"
  "${SEEKDB_PACKAGE_PROFILE_DIR}/postinst"
  "${SEEKDB_PACKAGE_PROFILE_DIR}/prerm"
  "${SEEKDB_PACKAGE_PROFILE_DIR}/postrm"
  DESTINATION usr/libexec/seekdb/scripts
  COMPONENT server)

message(STATUS "CPack generator: DEB")
message(STATUS "CPack components: ${CPACK_COMPONENTS_ALL}")

include(CPack)

add_custom_target(deb
  COMMAND "${CMAKE_CPACK_COMMAND}" -G DEB
    --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
  DEPENDS seekdb generate_syspack_source
  USES_TERMINAL
  VERBATIM)
